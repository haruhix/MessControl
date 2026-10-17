// Compiled together with ORIGINAL Source/MessControl/Private/MCCoffeeWipe.cpp.
// All state resets/allocations, file IO and result consumption are outside timing.
#include "MCCoffeeWipe.h"
#include "coffee_mask_resolution_recipe.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;
volatile double Sink = 0;
void Require(bool Value, const char* Message) {
    if(!Value) throw std::runtime_error(Message);
}
void WriteMask(const std::filesystem::path& File, const TArray<uint8>& Mask) {
    std::ofstream Out(File, std::ios::binary);
    Out.write(reinterpret_cast<const char*>(Mask.Data.data()), Mask.Num());
    Require(bool(Out), "Could not write mask");
}
template<class Reset, class Work> std::vector<double> Measure(Reset Prepare, Work Run) {
    for(int I=0; I<24; ++I) { Prepare(); Sink = Run(); }
    std::vector<double> Result;
    Result.reserve(240);
    for(int Batch=0; Batch<8; ++Batch) for(int I=0; I<30; ++I) {
        Prepare();
        const auto Begin=Clock::now();
        const double Value=Run();
        const auto End=Clock::now();
        Sink = Value;
        Result.push_back(std::chrono::duration<double,std::micro>(End-Begin).count());
    }
    return Result;
}
void Samples(std::ofstream& Out, const char* Name, const std::vector<double>& Values, bool Comma) {
    Out << (Comma ? ",\n" : "") << '"' << Name << "\":[";
    for(size_t I=0; I<Values.size(); ++I) Out << (I ? "," : "") << Values[I];
    Out << ']';
}
}

int main(int Argc, char** Argv) {
    try {
        Require(Argc==2, "Pass output directory");
        const std::filesystem::path Output=std::filesystem::u8path(Argv[1]);
        std::filesystem::create_directories(Output);
        TArray<uint8> Before, Deposited;
        TArray<float> Fractional;
        Before.Init(0,FMCCoffeeWipe::Count); Deposited.Init(0,FMCCoffeeWipe::Count);
        for(const auto& Segment:CoffeeRecipe::Deposits)
            FMCCoffeeWipe::Deposit(Before,Deposited,Segment.From,Segment.To,Segment.Radius);
        Require(Before.Num()==FMCCoffeeWipe::Count && FMCCoffeeWipe::HasWetCells(Before), "No generated deposit");
        TArray<uint8> After=Before;
        Fractional.Init(0,FMCCoffeeWipe::Count);
        for(int Pass=0; Pass<CoffeeRecipe::BrushPasses; ++Pass)
            for(const auto& Segment:CoffeeRecipe::Brush)
                FMCCoffeeWipe::Stroke(After,Segment.From,Segment.To,Segment.Radius,
                    CoffeeRecipe::Seconds,&Fractional,CoffeeRecipe::Rate);
        int64 BeforeSum=0,AfterSum=0;
        for(int I=0; I<FMCCoffeeWipe::Count; ++I) {
            Require(After[I]<=Before[I], "Stroke created new coverage");
            BeforeSum+=Before[I];AfterSum+=After[I];
        }
        Require(AfterSum<BeforeSum, "Fixed brush path cleared nothing");
        WriteMask(Output/"before.mask",Before);WriteMask(Output/"after.mask",After);

        TArray<uint8> Work,WorkDeposit,Zero;
        TArray<float> WorkFractional;
        Zero.Init(0,FMCCoffeeWipe::Count);
        WorkFractional.Init(0,FMCCoffeeWipe::Count);
        const auto Stroke=CoffeeRecipe::Brush[CoffeeRecipe::Brush.size()/2];
        const auto Deposit=CoffeeRecipe::Deposits[2];
        auto StrokeTimes=Measure([&] {Work=Before;WorkFractional.Init(0,FMCCoffeeWipe::Count);},[&] {
            return double(FMCCoffeeWipe::Stroke(Work,Stroke.From,Stroke.To,Stroke.Radius,
                CoffeeRecipe::Seconds,&WorkFractional,CoffeeRecipe::Rate));
        });
        auto DepositTimes=Measure([&] {Work.Init(0,FMCCoffeeWipe::Count);WorkDeposit.Init(0,FMCCoffeeWipe::Count);},[&] {
            return double(FMCCoffeeWipe::Deposit(Work,WorkDeposit,Deposit.From,Deposit.To,Deposit.Radius));
        });
        auto RemainingTimes=Measure([] {},[&] {return double(FMCCoffeeWipe::Remaining(After));});
        auto HasWetZeroTimes=Measure([] {},[&] {return double(FMCCoffeeWipe::HasWetCells(Zero));});
        std::ofstream Out(Output/"timings.json");Out << std::setprecision(10) << "{\n";
        Out << "\"resolution\":" << FMCCoffeeWipe::Size << ",\"repetitions\":240,\"batches\":8,\"warmup_each\":24,\"unit\":\"microseconds_per_call\",\n\"samples\":{";
        Samples(Out,"stroke_one_short_segment",StrokeTimes,false);
        Samples(Out,"deposit_one_capsule",DepositTimes,true);
        Samples(Out,"remaining_full_array_scan",RemainingTimes,true);
        Samples(Out,"has_wet_zero_full_array_scan",HasWetZeroTimes,true);
        Out << "},\n\"before_sum\":" << BeforeSum << ",\"after_sum\":" << AfterSum << ",\"sink\":" << Sink << "}\n";
        Require(bool(Out), "Could not write timings");
        std::cout << "N=" << FMCCoffeeWipe::Size << " before=" << BeforeSum << " after=" << AfterSum << '\n';
    } catch(const std::exception& Error) {
        std::cerr << Error.what() << '\n';return 1;
    }
}
