// Offline experiment only. Native MCCoffeeWipe.cpp is linked unchanged.
// The candidate include is generated from that exact Stroke body: loop bounds
// change, plus a separately measured optional byte-sum/wet-count accumulator.
#include "MCCoffeeWipe.h"
#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

#define COFFEE_NOINLINE __declspec(noinline)
struct FByteAggregates { int64 Sum=0; int32 Wet=0,Nonzero=0; };
COFFEE_NOINLINE FByteAggregates ScanAggregates(const TArray<uint8>& Mask) {
    FByteAggregates Out;
    for(uint8 V:Mask) {Out.Sum+=V;Out.Wet+=V>63;Out.Nonzero+=V>0;}
    return Out;
}
#include "coffee_wipe_bounded_candidates.inc"

namespace {
using Clock=std::chrono::steady_clock;
volatile std::uint64_t Sink=0;
struct FStroke { FVector2D From,To;float Radius=.045f,Seconds=1.f/60.f,Rate=1.f; };
struct FParityStats {int32 Steps=0,Sequences=0,InvalidOrNoop=0,WithoutFractional=0,Resets=0;};
void Require(bool Value,const std::string& Message) {if(!Value) throw std::runtime_error(Message);}
bool Same(const FByteAggregates& A,const FByteAggregates& B) {return A.Sum==B.Sum && A.Wet==B.Wet && A.Nonzero==B.Nonzero;}
std::uint64_t Digest(const TArray<uint8>& Mask,const TArray<float>& Fractional,const FByteAggregates& Aggregate,bool Changed) {
    std::uint64_t Hash=1469598103934665603ull;
    for(uint8 V:Mask) Hash=(Hash^V)*1099511628211ull;
    for(float V:Fractional) Hash=(Hash^std::bit_cast<std::uint32_t>(V))*1099511628211ull;
    return Hash^std::uint64_t(Aggregate.Sum)^std::uint64_t(Aggregate.Wet)^std::uint64_t(Changed);
}
TArray<uint8> RecipeMask() {
    TArray<uint8> Mask,Ledger;Mask.Init(0,FMCCoffeeWipe::Count);Ledger.Init(0,FMCCoffeeWipe::Count);
    const FVector2D Main[]={{.12,.34},{.25,.24},{.40,.30},{.53,.44},{.67,.35},{.82,.48},{.83,.66}};
    const FVector2D BranchA[]={{.28,.29},{.25,.59},{.35,.76}},BranchB[]={{.57,.43},{.65,.67},{.78,.77}};
    for(int I=1;I<7;++I) FMCCoffeeWipe::Deposit(Mask,Ledger,Main[I-1],Main[I],.12f);
    for(int I=1;I<3;++I) {
        FMCCoffeeWipe::Deposit(Mask,Ledger,BranchA[I-1],BranchA[I],.04f);
        FMCCoffeeWipe::Deposit(Mask,Ledger,BranchB[I-1],BranchB[I],.04f);
    }
    return Mask;
}
void CheckSequence(const TArray<uint8>& Initial,const TArray<float>& InitialFractional,
                   const std::vector<FStroke>& Strokes,bool UseFractional,FParityStats& Stats) {
    TArray<uint8> A=Initial,B=Initial,C=Initial;
    TArray<float> FA=InitialFractional,FB=FA,FC=FA;
    auto Aggregate=ScanAggregates(C);
    ++Stats.Sequences;
    for(const auto& S:Strokes) {
        const bool CA=FMCCoffeeWipe::Stroke(A,S.From,S.To,S.Radius,S.Seconds,UseFractional?&FA:nullptr,S.Rate);
        const bool CB=StrokeBounded(B,S.From,S.To,S.Radius,S.Seconds,UseFractional?&FB:nullptr,S.Rate);
        const bool CC=StrokeBoundedTracked(C,S.From,S.To,S.Radius,S.Seconds,UseFractional?&FC:nullptr,S.Rate,&Aggregate);
        Require(CA==CB && CA==CC,"Changed return differs at step "+std::to_string(Stats.Steps));
        Require(A.Data==B.Data && A.Data==C.Data,"Coverage bytes differ at step "+std::to_string(Stats.Steps));
        Require(FA.Num()==FB.Num() && FA.Num()==FC.Num(),"Fractional count differs");
        for(int I=0;I<FA.Num();++I)
            Require(std::bit_cast<std::uint32_t>(FA[I])==std::bit_cast<std::uint32_t>(FB[I]) &&
                    std::bit_cast<std::uint32_t>(FA[I])==std::bit_cast<std::uint32_t>(FC[I]),"Fractional bits differ");
        const auto Full=ScanAggregates(A);
        Require(Same(Full,Aggregate),"Incremental aggregate differs");
        Require((Aggregate.Wet>0)==FMCCoffeeWipe::HasWetCells(A),"Wet-count predicate differs");
        if(A.Num()==FMCCoffeeWipe::Count)
            Require(float(Aggregate.Sum)/(255*FMCCoffeeWipe::Count)==FMCCoffeeWipe::Remaining(A),"Unweighted Remaining differs");
        if(!CA) ++Stats.InvalidOrNoop;
        if(!UseFractional) ++Stats.WithoutFractional;
        ++Stats.Steps;
    }
    Sink=Sink^Digest(A,FA,Aggregate,false);
}
FParityStats Verify() {
    FParityStats Stats;
    const float NaN=std::numeric_limits<float>::quiet_NaN(),Inf=std::numeric_limits<float>::infinity();
    std::vector<FStroke> Edges={
        {{.5,.5},{.5,.5},.01f},{{0,0},{0,0},.045f},{{1,1},{1,1},.045f},
        {{0,.5},{1,.5},.045f},{{1,.5},{0,.5},.045f},{{-.2,.4},{1.2,.6},.045f},
        {{.02,.02},{.98,.98},.045f},{{.98,.02},{.02,.98},.045f},
        {{.5,.5},{.51,.49},.35f},{{.5,.5},{.51,.49},.8f,.2f,2.f},
        {{.5,.5},{.51,.49},.0001f,.0001f,.2f},
        {{-.3,-.3},{-.2,-.2},.045f},{{1.2,1.2},{1.3,1.3},.045f},
        {{NaN,.5},{.5,.5},.045f},{{.5,.5},{Inf,.5},.045f},
        {{.5,.5},{.6,.5},NaN},{{.5,.5},{.6,.5},Inf},{{.5,.5},{.6,.5},0},
        {{.5,.5},{.6,.5},-.1f},{{.5,.5},{.6,.5},.045f,NaN},
        {{.5,.5},{.6,.5},.045f,Inf},{{.5,.5},{.6,.5},.045f,0},
        {{.5,.5},{.6,.5},.045f,-1},{{.5,.5},{.6,.5},.045f,.01f,NaN},
        {{.5,.5},{.6,.5},.045f,.01f,Inf}};
    std::mt19937 Random(0x43574631u);
    TArray<uint8> Pattern,Zero,Full;Pattern.Init(0,FMCCoffeeWipe::Count);Zero=Pattern;Full.Init(255,FMCCoffeeWipe::Count);
    TArray<float> Fractional,Empty;Fractional.Init(0,FMCCoffeeWipe::Count);
    const uint8 ThresholdValues[]={0,1,62,63,64,100,180,254,255};
    for(int I=0;I<Pattern.Num();++I) {Pattern[I]=ThresholdValues[Random()%9];Fractional[I]=float(Random()%1000)/1000.f;}
    for(const auto& Mask:{Pattern,Zero,Full,RecipeMask()})
        for(bool UseFractional:{false,true}) CheckSequence(Mask,Fractional,Edges,UseFractional,Stats);
    // Both early rejection with invalid arrays and the native Reset path.
    TArray<uint8> Short;Short.Init(17,3);
    for(const auto& Mask:{TArray<uint8>(),Short}) {
        CheckSequence(Mask,Empty,{Edges[13],Edges[11]},true,Stats);
        CheckSequence(Mask,Empty,{Edges[0],Edges[6]},true,Stats);++Stats.Resets;
    }
    // One shared mask/fractional state; four players have independent previous
    // positions, while their strokes are applied in the same interleaved order.
    std::vector<FStroke> Interleaved;
    FVector2D Previous[4]={{.2,.2},{.8,.2},{.2,.8},{.8,.8}};
    for(int Step=0;Step<160;++Step) {
        const int Player=(Step*3)%4;
        const double X=double(Random()%14001)/10000.-.2,Y=double(Random()%14001)/10000.-.2;
        const FVector2D Next(X,Y);
        Interleaved.push_back({Previous[Player],Next,.01f+float(Random()%900)/10000.f,
            .001f+float(Random()%120)/1000.f,.5f+float(Random()%200)/100.f});Previous[Player]=Next;
    }
    CheckSequence(Pattern,Fractional,Interleaved,true,Stats);
    CheckSequence(RecipeMask(),Empty,Interleaved,false,Stats);
    // Repeated point work forces fractional carry, threshold crossings and dry completion.
    CheckSequence(Full,Empty,std::vector<FStroke>(180,{{.5,.5},{.5,.5},.1f,.1f,2.f}),true,Stats);
    return Stats;
}
using FRun=bool(*)(TArray<uint8>&,const FStroke&,TArray<float>&,FByteAggregates&);
COFFEE_NOINLINE bool RunA(TArray<uint8>& M,const FStroke& S,TArray<float>& F,FByteAggregates&) {
    return FMCCoffeeWipe::Stroke(M,S.From,S.To,S.Radius,S.Seconds,&F,S.Rate);
}
COFFEE_NOINLINE bool RunB(TArray<uint8>& M,const FStroke& S,TArray<float>& F,FByteAggregates&) {
    return StrokeBounded(M,S.From,S.To,S.Radius,S.Seconds,&F,S.Rate);
}
COFFEE_NOINLINE bool RunBScan(TArray<uint8>& M,const FStroke& S,TArray<float>& F,FByteAggregates& G) {
    const bool Changed=StrokeBounded(M,S.From,S.To,S.Radius,S.Seconds,&F,S.Rate);G=ScanAggregates(M);return Changed;
}
COFFEE_NOINLINE bool RunC(TArray<uint8>& M,const FStroke& S,TArray<float>& F,FByteAggregates& G) {
    return StrokeBoundedTracked(M,S.From,S.To,S.Radius,S.Seconds,&F,S.Rate,&G);
}
struct FPair {std::vector<double> A,B;};
FPair MeasurePair(const TArray<uint8>& Before,const FStroke& Stroke,FRun Left,FRun Right) {
    TArray<uint8> Work=Before;TArray<float> Fractional;Fractional.Init(0,FMCCoffeeWipe::Count);
    const auto Initial=ScanAggregates(Before);FByteAggregates Aggregate=Initial;
    auto Sample=[&](FRun Run,bool Timed) {
        Work=Before;Fractional.Init(0,FMCCoffeeWipe::Count);Aggregate=Initial;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto Begin=Clock::now();
        const bool Changed=Run(Work,Stroke,Fractional,Aggregate);
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto End=Clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        Sink=Sink^Digest(Work,Fractional,Aggregate,Changed);
        return Timed?std::chrono::duration<double,std::micro>(End-Begin).count():0.;
    };
    for(int I=0;I<24;++I) {Sample(Left,false);Sample(Right,false);}
    FPair Out;Out.A.reserve(240);Out.B.reserve(240);
    for(int Batch=0;Batch<8;++Batch) for(int I=0;I<30;++I) {
        // Alternate the immediately paired order; batching never compares all A
        // samples before all B samples. Resets and checksum are outside timing.
        if((Batch+I)%2==0) {Out.A.push_back(Sample(Left,true));Out.B.push_back(Sample(Right,true));}
        else {Out.B.push_back(Sample(Right,true));Out.A.push_back(Sample(Left,true));}
    }
    return Out;
}
void Values(std::ostream& Out,const std::vector<double>& V) {
    Out<<'[';for(size_t I=0;I<V.size();++I) Out<<(I?",":"")<<V[I];Out<<']';
}
void CorrectnessJson(std::ostream& Out,const FParityStats& S) {
    Out<<"{\"passed\":true,\"resolution\":"<<FMCCoffeeWipe::Size<<",\"steps\":"<<S.Steps
       <<",\"sequences\":"<<S.Sequences<<",\"invalid_or_noop\":"<<S.InvalidOrNoop
       <<",\"without_fractional\":"<<S.WithoutFractional<<",\"reset_sequences\":"<<S.Resets
       <<",\"mask_byte_parity\":true,\"fractional_bit_parity\":true,\"changed_return_parity\":true"
       <<",\"aggregate_sum_wet_nonzero_parity\":true,\"four_player_interleaving\":true}";
}
}
int main(int Argc,char** Argv) {
    try {
        Require(Argc==3,"Usage: coffee_wipe_bounded_ab verify|benchmark output.json");
        const std::string Mode=Argv[1];Require(Mode=="verify" || Mode=="benchmark","Invalid mode");
        const auto Verified=Verify();std::ofstream Out(std::filesystem::u8path(Argv[2]));Require(bool(Out),"Cannot open output");
        Out<<std::setprecision(12);
        if(Mode=="verify") {CorrectnessJson(Out,Verified);Out<<'\n';}
        else {
            const TArray<uint8> Before=RecipeMask();
            const std::pair<const char*,FStroke> Fixtures[]={
                {"point_small",{{.4,.3},{.4,.3},.035f}},
                {"short_diagonal",{{.4818374248371735,.43010160947676657},{.49717753213562466,.4205140424152346},.045f}},
                {"long_diagonal",{{.1,.1},{.9,.9},.045f}},
                {"edge_crossing",{{-.02,.30},{.22,.28},.045f}},
                {"broad_radius",{{.2,.2},{.8,.8},.35f}},
                {"outside_noop",{{-.5,-.5},{-.2,-.2},.04f}}};
            Out<<"{\"resolution\":"<<FMCCoffeeWipe::Size<<",\"unit\":\"microseconds_per_call\",\"correctness\":";
            CorrectnessJson(Out,Verified);Out<<",\"warmups_each\":24,\"batches\":8,\"samples_each\":240,\"fixtures\":[";
            bool Comma=false;
            for(const auto& Fixture:Fixtures) {
                const auto Pair=MeasurePair(Before,Fixture.second,RunA,RunB);
                const auto Aggregate=MeasurePair(Before,Fixture.second,RunBScan,RunC);
                Out<<(Comma?",":"")<<"{\"name\":\""<<Fixture.first<<"\",\"from\":["<<Fixture.second.From.X<<','<<Fixture.second.From.Y
                   <<"],\"to\":["<<Fixture.second.To.X<<','<<Fixture.second.To.Y<<"],\"radius_uv\":"<<Fixture.second.Radius
                   <<",\"seconds\":"<<Fixture.second.Seconds<<",\"rate\":"<<Fixture.second.Rate<<",\"full_scan_A\":";
                Values(Out,Pair.A);Out<<",\"bounded_B\":";Values(Out,Pair.B);
                Out<<",\"bounded_plus_aggregate_scan\":";Values(Out,Aggregate.A);
                Out<<",\"bounded_incremental_aggregates\":";Values(Out,Aggregate.B);Out<<'}';Comma=true;
            }
            Out<<"],\"sink\":"<<Sink<<"}\n";
        }
        Require(bool(Out),"Cannot write output");std::cout<<"PASS N="<<FMCCoffeeWipe::Size<<" mode="<<Mode<<" parity_steps="<<Verified.Steps<<'\n';
    } catch(const std::exception& Error) {std::cerr<<Error.what()<<'\n';return 1;}
}
