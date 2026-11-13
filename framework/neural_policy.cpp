#include "headers/neural_policy.h"

namespace NeuralPolicy {

double Evaluate(const Model& model, const double* features) {
    double score = model.b2;
    for (size_t hidden = 0; hidden < model.hidden; ++hidden) {
        double value = model.b1[hidden];
        const double* row = &model.w1[hidden * model.inputs];
        for (size_t input = 0; input < model.inputs; ++input)
            value += row[input] * features[input];
        if (value > 0.0) score += model.w2[hidden] * value;
    }
    return score;
}

}
