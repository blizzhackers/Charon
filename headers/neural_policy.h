#pragma once

#include <array>
#include <cstddef>

namespace NeuralPolicy {

constexpr size_t MaxInputs = 64;
constexpr size_t MaxHidden = 64;
constexpr size_t MaxCandidates = 64;
constexpr double MaxWeight = 1000.0;

using Features = std::array<double, MaxInputs>;

// inputs -> hidden ReLU -> 1 linear. Each model carries its own shape; only
// the first `hidden * inputs` entries of w1 (hidden-row major) are used.
struct Model {
    size_t inputs;
    size_t hidden;
    std::array<double, MaxHidden * MaxInputs> w1;
    std::array<double, MaxHidden> b1;
    std::array<double, MaxHidden> w2;
    double b2;
};

// `features` holds at least model.inputs values.
double Evaluate(const Model& model, const double* features);

}
