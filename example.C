#include "RExplodeDS.hxx"

// PLEASE NOTE !!!!
// This is a demonstrator to focus on functionality: performance has not been optimised yet!
// The API will be subject to changes

// Begin preparation
std::vector<ULong64_t> generateVector(ULong64_t iEvt)
{
   std::vector<ULong64_t> v;v.reserve(iEvt);
   for (auto i : ROOT::TSeqU(iEvt))
      v.emplace_back(i);
   return v;
}

void PrintContent(ULong64_t i, const std::vector<ULong64_t> &j)
{
   std::cout << i << " ";
   for (auto &&v : j)
      std::cout << v << " ";
   std::cout << endl;
}

ROOT::RDF::RNode PrepareDF()
{
   ROOT::RDataFrame df(16);
   auto df2 = df.Define("i", [](ULong64_t i) { return i; }, {"rdfentry_"})
                .Define("v", generateVector, {"i"});
   return df2;
}

// End preparation

// PLEASE NOTE !!!!
// This is a demonstrator to focus on functionality: performance has not been optimised yet!
// The API will be subject to changes

void example()
{
   // Prepare a dummy dataset with two columns i and v. 
   // Store an unsigned long int in i and a vector of that type in v, filled
   // more or less at random
   auto df = PrepareDF();

   // Extract columns to feed later the exploded DF constructor
   auto coli = df.Take<ULong64_t>("i");
   auto colv = df.Take<vector<ULong64_t>>("v");

   // this statement is there just to print the content of the df
   df.Foreach(PrintContent, {"i", "v"});

   // Now build the "exploded" dataframe and dump it!
   auto expdf = ROOT::RDF::MakeExplodedDataFrame(std::make_pair(std::string("i"), coli),
                                                 std::make_pair(std::string("v"), colv));
   expdf.Foreach([](ULong64_t i, ULong64_t j) { cout << i << " " << j << endl; }, {"i", "v"});
}