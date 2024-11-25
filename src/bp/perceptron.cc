#include "perceptron.h"


#include <vector>

extern "C" {
#include "bp/bp.param.h"
#include "core.param.h"
#include "globals/assert.h"
#include "statistics.h"
}

//#define PERCEPTRON_TABLE_SIZE 1024          // Number of perceptrons
#define PERCEPTRON_THRESHOLD ((int)1.93 * HIST_LENGTH + 14)            // PERCEPTRON_THRESHOLD for updating perceptron weights
// #define HIST_LENGTH 59              // Length of the Global History Register (GHR)
// #define WEIGHT_BITS 8            // Number of bits to store each weight
#define MAX_WEIGHT 127           // Maximum weight value
#define MIN_WEIGHT -128          // Minimum weight value
#define PERCEPTRON_INIT_WEIGHT 0 // Initial weight value
#define TAKEN 1
#define NOT_TAKEN 0

#define DEBUG(proc_id, args...) _DEBUG(proc_id, DEBUG_BP_DIR, ##args)

namespace {

struct Perceptron_Params {
    std::vector<int8_t> weights;
};

struct Perceptron_State {
    std::vector<Perceptron_Params> perceptron_table;
};

std::vector<Perceptron_State> perceptron_state_all_cores;

// Hash function to map PC and GHR to perceptron table index
inline uint32_t get_perceptron_index(const uint32_t pc, const uint64_t ghr) {
    uint32_t pc_index = (pc >> 2) & (PERCEPTRON_TABLE_SIZE - 1); // Assuming 4-byte alignment
    uint32_t ghr_index = (static_cast<uint32_t>(ghr) & (PERCEPTRON_TABLE_SIZE - 1));
    return pc_index ^ ghr_index;
}
}//namespace

void bp_perceptron_timestamp(Op* op) {}
void bp_perceptron_recover(Recovery_Info* info) {}
void bp_perceptron_spec_update(Op* op) {}
void bp_perceptron_retire(Op* op) {}
uns8 bp_perceptron_full(unsigned proc_id) { return 0; }

// Initialize the perceptron predictor
void bp_perceptron_init() {
    perceptron_state_all_cores.resize(NUM_CORES);
    for(auto& state : perceptron_state_all_cores) {
        state.perceptron_table.resize(PERCEPTRON_TABLE_SIZE);
        for(auto& perceptron : state.perceptron_table) {
            perceptron.weights.resize(HIST_LENGTH + 1);
            for(uint32_t i = 0; i < HIST_LENGTH + 1; ++i) {
                perceptron.weights[i] = PERCEPTRON_INIT_WEIGHT;
            }
        }
    }
}

// Predict the branch direction using perceptron
uns8 bp_perceptron_pred(Op* op) {
    const unsigned proc_id = op->proc_id;
    const auto& state = perceptron_state_all_cores.at(proc_id).perceptron_table;

    const uint32_t pc = op->oracle_info.pred_addr;
    const uint64_t ghr = op->oracle_info.pred_global_hist;

    uint32_t index = get_perceptron_index(pc, ghr);
    const Perceptron_Params& perceptron = state[index];

    int32_t prediction = perceptron.weights[0]; // Bias weight
    for(uint32_t i = 0; i < HIST_LENGTH; ++i) {
        if((ghr >> i) & 1) { // If history bit is taken
            prediction += perceptron.weights[i + 1];
        } else { // If history bit is not taken
            prediction -= perceptron.weights[i + 1];
        }
    }

    // Store absolute prediction for potential updating
    op->perceptron_output = std::abs(prediction);

    bool pred_dir = (prediction >= 0) ? TAKEN : NOT_TAKEN;

    DEBUG(proc_id, "Perceptron Prediction for op_num:%s at index:%u: pred=%d (%s)\n",
          unsstr64(op->op_num), index, prediction, pred_dir ? "TAKEN" : "NOT_TAKEN");

    return pred_dir;
}

// Update the perceptron predictor based on actual branch outcome
void bp_perceptron_update(Op* op) {
    if(op->table_info->cf_type != CF_CBR) {
        // Non-conditional branches do not affect the perceptron predictor
        return;
    }

    const unsigned proc_id = op->proc_id;
    auto& state = perceptron_state_all_cores.at(proc_id).perceptron_table;

    const uint32_t pc = op->oracle_info.pred_addr;
    const uint64_t ghr = op->oracle_info.pred_global_hist;
    const bool actual_dir = op->oracle_info.dir;

    uint32_t index = get_perceptron_index(pc, ghr);
    Perceptron_Params& perceptron = state[index];

    // Calculate the prediction again to get the prediction value
    int32_t prediction = perceptron.weights[0]; // Bias weight
    for(uint32_t i = 0; i < HIST_LENGTH; ++i) {
        if((ghr >> i) & 1) { // If history bit is taken
            prediction += perceptron.weights[i + 1];
        } else { // If history bit is not taken
            prediction -= perceptron.weights[i + 1];
        }
    }

    bool pred_dir = (prediction >= 0) ? TAKEN : NOT_TAKEN;

    // Decide whether to update the perceptron
    if(pred_dir != actual_dir || std::abs(prediction) <= PERCEPTRON_THRESHOLD) {
        // Update bias
        if(actual_dir == TAKEN) {
            if(perceptron.weights[0] < MAX_WEIGHT) {
                perceptron.weights[0]++;
            }
        } else {
            if(perceptron.weights[0] > MIN_WEIGHT) {
                perceptron.weights[0]--;
            }
        }

        // Update weights based on history
        for(uint32_t i = 0; i < HIST_LENGTH; ++i) {
            bool history_bit = (ghr >> i) & 1;
            if((actual_dir == TAKEN && history_bit) || (actual_dir == NOT_TAKEN && !history_bit)) {
                // If the branch outcome matches the history bit, increment the weight
                if(perceptron.weights[i + 1] < MAX_WEIGHT) {
                    perceptron.weights[i + 1]++;
                }
            } else {
                // If the branch outcome does not match the history bit, decrement the weight
                if(perceptron.weights[i + 1] > MIN_WEIGHT) {
                    perceptron.weights[i + 1]--;
                }
            }
        }

        DEBUG(proc_id, "Updated Perceptron at index:%u with actual_dir=%d\n", index, actual_dir);
    }

    // Update the Global History Register (GHR)
    // Shift left and insert the new outcome
    uint64_t new_ghr = (ghr << 1) | (actual_dir ? 1 : 0);
    op->oracle_info.pred_global_hist = new_ghr & ((1ULL << HIST_LENGTH) - 1); // Keep only HIST_LENGTH bits

    DEBUG(proc_id, "GHR updated to %llu\n", op->oracle_info.new_pred_global_hist);
}