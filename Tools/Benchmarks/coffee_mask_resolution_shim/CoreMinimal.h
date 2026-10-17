#pragma once
// Standalone adapter only. The production MCCoffeeWipe.cpp is compiled as-is.
// This is not UE Core, its allocator, or a claim of bit-exact engine libm output.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
#ifndef MESSCONTROL_API
#define MESSCONTROL_API
#endif
template<class Signature> using TFunction = std::function<Signature>;
template<class T> struct TArray {
    std::vector<T> Data;
    int32 Num() const { return static_cast<int32>(Data.size()); }
    void Init(T Value, int32 Count) { Data.assign(Count, Value); }
    T& operator[](int32 Index) { return Data[Index]; }
    const T& operator[](int32 Index) const { return Data[Index]; }
    auto begin() { return Data.begin(); }
    auto end() { return Data.end(); }
    auto begin() const { return Data.begin(); }
    auto end() const { return Data.end(); }
};
struct FVector2D {
    double X = 0, Y = 0;
    FVector2D() = default;
    explicit FVector2D(double Both) : X(Both), Y(Both) {}
    FVector2D(double InX, double InY) : X(InX), Y(InY) {}
    FVector2D operator+(FVector2D B) const { return {X+B.X, Y+B.Y}; }
    FVector2D operator-(FVector2D B) const { return {X-B.X, Y-B.Y}; }
    FVector2D operator*(double Scale) const { return {X*Scale, Y*Scale}; }
    FVector2D operator/(double Scale) const { return {X/Scale, Y/Scale}; }
    bool ContainsNaN() const { return !std::isfinite(X) || !std::isfinite(Y); }
    double SizeSquared() const { return X*X+Y*Y; }
    double Size() const { return std::sqrt(SizeSquared()); }
    static double DotProduct(FVector2D A, FVector2D B) { return A.X*B.X+A.Y*B.Y; }
};
struct FMath {
    template<class T> static T Min(T A, T B) { return A < B ? A : B; }
    template<class T> static T Max(T A, T B) { return A > B ? A : B; }
    template<class T> static T Clamp(T X, T A, T B) { return X < A ? A : X < B ? X : B; }
    template<class T> static T Abs(T A) { return std::abs(A); }
    template<class T> static bool IsFinite(T X) { return std::isfinite(X); }
    template<class T> static T Sin(T X) { return std::sin(X); }
    template<class T> static T Cos(T X) { return std::cos(X); }
    template<class T> static T Exp(T X) { return std::exp(X); }
    template<class T> static int32 FloorToInt(T X) { return static_cast<int32>(std::floor(X)); }
    template<class T> static int32 CeilToInt(T X) { return static_cast<int32>(std::ceil(X)); }
    static int32 RoundToInt(float X) { return FloorToInt(X+.5f); }
    static int64 RoundToInt(double X) { return static_cast<int64>(std::floor(X+.5)); }
    template<class T, class U> static auto Lerp(T A, T B, U Alpha) { return A+Alpha*(B-A); }
    template<class T> static T SmoothStep(T A, T B, T X) {
        if(X < A) return 0;
        if(X >= B) return 1;
        const T Fraction = (X-A)/(B-A);
        return Fraction*Fraction*(3.0f-2.0f*Fraction);
    }
};
