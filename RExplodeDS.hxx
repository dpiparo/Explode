// Author: Danilo Piparo CERN  01/2019

/*************************************************************************
 * Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

// This is a Prototype! Do not put in production!!!

#ifndef ROOT_REXPLODEDS
#define ROOT_REXPLODEDS

#include "ROOT/RIntegerSequence.hxx"
#include "ROOT/RMakeUnique.hxx"
#include "ROOT/RDataSource.hxx"
#include "ROOT/RResultPtr.hxx"
#include "ROOT/TSeq.hxx"

#include <algorithm>
#include <map>
#include <tuple>
#include <string>
#include <typeinfo>
#include <vector>

namespace ROOT {

namespace Internal {
namespace RDF {

std::string GetContainedName(const std::string &collName)
{
   int dummy;
   std::vector<std::string> splitName;
   TClassEdit::GetSplit(collName.c_str(),
		                  splitName,
                        dummy);
   return splitName[1];
}

template <typename>
struct IsVectorOrRVec : std::false_type {
};

template <typename T>
struct IsVectorOrRVec<std::vector<std::vector<T>>> : std::true_type
{
};

template <typename T>
struct IsVectorOrRVec < std::vector<ROOT::RVec<T>>> : std::true_type
{
};

template <typename T>
struct InnerType
{
   using type = T;
};

template <typename T>
struct InnerType<std::vector<T>>
{
   using type = T;
};

template <typename T>
struct InnerType<ROOT::RVec<T>>
{
   using type = T;
};

template <typename T>
const T &GetAt(const T &v, unsigned int) {
   return v;
   }

template <typename T>
const T &GetAt(const std::vector <T> & v, unsigned int i)
{
   return v.at(i); 
}

template <typename T>
const T &GetAt(const ROOT::RVec<T> &v, unsigned int i)
{
   return v.at(i); 
}

template <typename V>
void GetNCandidates(size_t &nCandidates, const V &values, std::vector<ULong64_t> &thresholds, std::true_type /*Is nested coll*/)
{
   size_t localNCandidates(0ULL);
   const auto mustFillThresholds = thresholds.empty();
   if (mustFillThresholds) {
      thresholds.reserve(values.size());
   }
   for (auto &&value : values) {
      localNCandidates += value.size();
      if (mustFillThresholds) {
         thresholds.emplace_back(localNCandidates);
      }
   }
   if (localNCandidates > nCandidates)
      nCandidates = localNCandidates;
}

template <typename V>
void GetNCandidates(size_t &nCandidates, const V &values, std::vector<ULong64_t>&, std::false_type /*Is not nested coll*/)
{
   size_t localNCandidates(values.size());
   if (localNCandidates > nCandidates)
      nCandidates = localNCandidates;
}

} // namespace RDF
} // namespace Internal

////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief A RDataSource implementation which allows to loop on candidates rather than entries
///
/// The implementation takes care of matching compile time information with runtime
/// information, e.g. expanding in a smart way the template parameters packs.
template <typename... ColumnTypes>
class RExplodeDS final : public ROOT::RDF::RDataSource {
   using PointerHolderPtrs_t = std::vector<ROOT::Internal::TDS::TPointerHolder *>;
   using ColumnsTuple_t = std::tuple<ROOT::RDF::RResultPtr<std::vector<ColumnTypes>>...>;
   ColumnsTuple_t fColumns;
   const std::vector<std::string> fColNames;
   const std::map<std::string, std::string> fColTypesMap;
   // The role of the fPointerHoldersModels is to be initialised with the pack
   // of arguments in the constrcutor signature at construction time
   // Once the number of slots is known, the fPointerHolders are initialised
   // according to the models.
   const PointerHolderPtrs_t fPointerHoldersModels;
   std::vector<PointerHolderPtrs_t> fPointerHolders;
   std::vector<std::pair<ULong64_t, ULong64_t>> fEntryRanges{};
   unsigned int fNSlots{0};
   std::vector<ULong64_t> fEvtThresholds;

   Record_t GetColumnReadersImpl(std::string_view colName, const std::type_info &id)
   {
      auto colNameStr = std::string(colName);
      // This could be optimised and done statically
      /*
      const auto idName = ROOT::Internal::RDF::TypeID2TypeName(id);
      auto it = fColTypesMap.find(colNameStr);
      if (fColTypesMap.end() == it) {
         std::string err = "The specified column name, \"" + colNameStr + "\" is not known to the data source.";
         throw std::runtime_error(err);
      }

      const auto colIdName = it->second;
      if (colIdName != idName) {
         std::string err = "Column " + colNameStr + " has type " + colIdName +
                           " while the id specified is associated to type " + idName;
         throw std::runtime_error(err);
      }
      */

      const auto colBegin = fColNames.begin();
      const auto colEnd = fColNames.end();
      const auto namesIt = std::find(colBegin, colEnd, colName);
      const auto index = std::distance(colBegin, namesIt);

      Record_t ret(fNSlots);
      for (auto slot : ROOT::TSeqU(fNSlots)) {
         ret[slot] = fPointerHolders[index][slot]->GetPointerAddr();
      }
      return ret;
   }

   template <std::size_t... S>
   size_t GetCandidatesNumberHelper(std::index_sequence<S...>)
   {
      using namespace ROOT::Internal::RDF;
      size_t nCandidates(0ULL);
      std::initializer_list<int> expander{
           (GetNCandidates(nCandidates,
                          *std::get<S>(fColumns).GetPtr(),
                          fEvtThresholds,
                          IsVectorOrRVec<typename std::tuple_element<S, ColumnsTuple_t>::type::Value_t>()), 0)...};
      (void)expander; // avoid unused variable warnings

      return nCandidates;
   }

   size_t GetEntriesNumber()
   {
      return GetCandidatesNumberHelper(std::index_sequence_for<ColumnTypes...>());
   }

   /// This returns the entry of the original dataset
   std::pair<ULong64_t, unsigned int> GetRealEntryNumberAndCollIndex(ULong64_t entry)
   {
      ULong64_t realEntry(0ULL);
      ULong64_t prevThreshold(0ULL);
      for (; entry >= fEvtThresholds[realEntry]; realEntry++)
      {
         prevThreshold = fEvtThresholds[realEntry];
      }

      const unsigned int idx = entry - prevThreshold;

      return std::make_pair(realEntry, idx);
   }

   template <std::size_t... S>
   void SetEntryHelper(unsigned int slot, ULong64_t entry, std::index_sequence<S...>)
   {
      using namespace ROOT::Internal::RDF; // for GetAt
      std::pair<ULong64_t, unsigned int> realEvt_collIdx;
      std::initializer_list<int> expander{
          (realEvt_collIdx = GetRealEntryNumberAndCollIndex(entry),
           *static_cast<typename InnerType<ColumnTypes>::type *>(fPointerHolders[S][slot]->GetPointer()) =
               GetAt((*std::get<S>(fColumns))[realEvt_collIdx.first], realEvt_collIdx.second),
           0)...};
      (void)expander; // avoid unused variable warnings
   }

   template <std::size_t... S>
   void ColLenghtChecker(std::index_sequence<S...>)
   {
      if (sizeof...(S) < 2)
         return;

      const std::vector<size_t> colLengths{std::get<S>(fColumns)->size()...};
      const auto expectedLen = colLengths[0];
      std::string err;
      for (auto i : TSeqI(1, colLengths.size())) {
         if (expectedLen != colLengths[i]) {
            err += "Column \"" + fColNames[i] + "\" and column \"" + fColNames[0] +
                   "\" have different lengths: " + std::to_string(expectedLen) + " and " +
                   std::to_string(colLengths[i]);
         }
      }
      if (!err.empty()) {
         throw std::runtime_error(err);
      }
   }

protected:
   std::string AsString() { return "explode data source"; };

public:
  RExplodeDS(std::pair<std::string, ROOT::RDF::RResultPtr<std::vector<ColumnTypes>>>... colsNameVals)
      : fColumns(std::tuple<ROOT::RDF::RResultPtr<std::vector<ColumnTypes>>...>(colsNameVals.second...)),
        fColNames({colsNameVals.first...}),
        fColTypesMap({{colsNameVals.first,
                       ROOT::Internal::RDF::GetContainedName(ROOT::Internal::RDF::TypeID2TypeName(typeid(ColumnTypes)))}...}),
        fPointerHoldersModels({new ROOT::Internal::TDS::TTypedPointerHolder<typename ROOT::Internal::RDF::InnerType<ColumnTypes>::type>(new typename ROOT::Internal::RDF::InnerType<ColumnTypes>::type())...})
  {
   }

   ~RExplodeDS()
   {
      for (auto &&ptrHolderv : fPointerHolders) {
         for (auto &&ptrHolder : ptrHolderv) {
            delete ptrHolder;
         }
      }
   }

   const std::vector<std::string> &GetColumnNames() const { return fColNames; }

   std::vector<std::pair<ULong64_t, ULong64_t>> GetEntryRanges()
   {
      auto entryRanges(std::move(fEntryRanges)); // empty fEntryRanges
      return entryRanges;
   }

   std::string GetTypeName(std::string_view colName) const
   {
      const auto key = std::string(colName);
      return fColTypesMap.at(key);
   }

   bool HasColumn(std::string_view colName) const
   {
      const auto key = std::string(colName);
      const auto endIt = fColTypesMap.end();
      return endIt != fColTypesMap.find(key);
   }

   bool SetEntry(unsigned int slot, ULong64_t entry)
   {
      SetEntryHelper(slot, entry, std::index_sequence_for<ColumnTypes...>());
      return true;
   }

   void SetNSlots(unsigned int nSlots)
   {
      fNSlots = nSlots;
      const auto nCols = fColNames.size();
      fPointerHolders.resize(nCols); // now we need to fill it with the slots, all of the same type
      auto colIndex = 0U;
      for (auto &&ptrHolderv : fPointerHolders) {
         for (auto slot : ROOT::TSeqI(fNSlots)) {
            auto ptrHolder = fPointerHoldersModels[colIndex]->GetDeepCopy();
            ptrHolderv.emplace_back(ptrHolder);
            (void)slot;
         }
         colIndex++;
      }
      for (auto &&ptrHolder : fPointerHoldersModels)
         delete ptrHolder;
   }

   void Initialise()
   {
      ColLenghtChecker(std::index_sequence_for<ColumnTypes...>());
      const auto nEntries = GetEntriesNumber();
      const auto nEntriesInRange = nEntries / fNSlots; // between integers. Should make smaller?
      auto reminder = 1U == fNSlots ? 0 : nEntries % fNSlots;
      fEntryRanges.resize(fNSlots);
      auto init = 0ULL;
      auto end = 0ULL;
      for (auto &&range : fEntryRanges) {
         end = init + nEntriesInRange;
         if (0 != reminder) { // Distribute the reminder among the first chunks
            reminder--;
            end += 1;
         }
         range.first = init;
         range.second = end;
         init = end;
      }
   }

   std::string GetLabel() { return "ExplodedDS"; }
};

namespace RDF {
template <typename... ColumnTypes>
RDataFrame MakeExplodedDataFrame(std::pair<std::string, ROOT::RDF::RResultPtr<std::vector<ColumnTypes>>> &&... colNameProxyPairs)
{
   RDataFrame rdf(std::make_unique<RExplodeDS<ColumnTypes...>>(
       std::forward<std::pair<std::string, ROOT::RDF::RResultPtr<std::vector<ColumnTypes>>>>(colNameProxyPairs)...));
   return rdf;
}
} // ns RDF

} // ns ROOT



#endif
