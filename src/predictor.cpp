//========================================================//
//  predictor.c                                           //
//  Source file for the Branch Predictor                  //
//                                                        //
//  Implement the various branch predictors below as      //
//  described in the README                               //
//========================================================//
#include "predictor.h"
#include <math.h>
#include <stdio.h>

//
// TODO:Student Information
//
const char *studentName = "Piyush Kumbhare";
const char *studentID = "A18151018";
const char *email = "pkumbhare@ucsd.edu";

//------------------------------------//
//      Predictor Configuration       //
//------------------------------------//

// Handy Global for use in output routines
const char *bpName[4] = {"Static", "Gshare", "Tournament", "Custom"};

// define number of bits required for indexing the BHT here.
// These will be a compile time constant in case we want to override

// gshare
#define GX_GLB_HIST_BITS 15 // Number of bits used for Global History

// tournament
#define TR_LOC_HIST_BITS 10 // Number of bits used for Local History
#define TR_LOC_MASK_BITS 10 // Number of PC bits to use to index into LHT
#define TR_GLB_HIST_BITS 12 // Number of bits used for Global History
#define TR_CHOOSER_BITS 12  // Number of bits used for chooser

int bpType; // Branch Prediction Type
int verbose;

//------------------------------------//
//      Predictor Data Structures     //
//------------------------------------//

//
// TODO: Add your own Branch Predictor data structures here
//

// used in gshare and tournament
uint64_t ghistory;

// gshare
uint8_t gshare_bht[1 << GX_GLB_HIST_BITS];

// tournament
uint8_t tr_chooser[1 << TR_CHOOSER_BITS]; // chooses between global and local
                                          // counter
uint16_t tr_lht[1 << TR_LOC_MASK_BITS];
uint8_t tr_glb_bht[1 << TR_GLB_HIST_BITS];
uint8_t tr_loc_bht[1 << TR_LOC_HIST_BITS];

//------------------------------------//
//        Predictor Functions         //
//------------------------------------//

static inline uint8_t sat_inc(uint8_t v, unsigned bits) {
  uint8_t max = (uint8_t)((1u << bits) - 1u);
  return (v < max) ? (v + 1u) : max;
}
static inline uint8_t sat_dec(uint8_t v, unsigned bits) {
  return (v > 0u) ? (v - 1u) : 0u;
}

// Initialize the predictor
//

void init_tournament() {
  size_t i;

  for (i = 0; i < (1 << TR_CHOOSER_BITS); i++)
    tr_chooser[i] = WL;

  for (i = 0; i < (1 << TR_LOC_HIST_BITS); i++)
    tr_loc_bht[i] = 0b011;

  for (i = 0; i < (1 << TR_GLB_HIST_BITS); i++)
    tr_glb_bht[i] = WT;

  for (i = 0; i < (1 << TR_LOC_MASK_BITS); i++)
    tr_lht[i] = 0x0;

  ghistory = 0;
}

uint8_t tournament_predict(uint32_t pc) {
  uint32_t chooser_index = ghistory & ((1 << TR_CHOOSER_BITS) - 1);
  switch (tr_chooser[chooser_index]) {
  case SG:
  case WG: {
    // Calculate global prediction
    uint32_t glb_bht_index = ghistory & ((1 << TR_GLB_HIST_BITS) - 1);

    return tr_glb_bht[glb_bht_index] >> 1;
  } break;
  case WL:
  case SL: {
    // Calculate local prediction
    uint32_t loc_bht_index = tr_lht[pc & ((1 << TR_LOC_MASK_BITS) - 1)] &
                             ((1 << TR_LOC_HIST_BITS) - 1);

    return tr_loc_bht[loc_bht_index] >> 2;
  } break;
  default:
    printf("Warning: Undefined state of entry in TOURNAMENT CHOOSER: %0.3b "
           "=> %d\n",
           tr_chooser[chooser_index], tr_chooser[chooser_index]);
    return NOTTAKEN;
  }
}

void train_tournament(uint32_t pc, uint8_t outcome) {
  uint32_t loc_bht_index = tr_lht[pc & ((1 << TR_LOC_MASK_BITS) - 1)] &
                           ((1 << TR_LOC_HIST_BITS) - 1);
  uint32_t glb_bht_index = ghistory & ((1 << TR_GLB_HIST_BITS) - 1);
  uint32_t chooser_index = ghistory & ((1 << TR_CHOOSER_BITS) - 1);

  uint8_t old_glb = tr_glb_bht[glb_bht_index];
  uint8_t glb_prediction = old_glb >> 1;
  uint8_t old_loc = tr_loc_bht[loc_bht_index];
  uint8_t loc_prediction = old_loc >> 2;

  // Update global bht
  tr_glb_bht[glb_bht_index] =
      outcome ? sat_inc(old_glb, 2) : sat_dec(old_glb, 2);

  // Update local bht
  tr_loc_bht[loc_bht_index] =
      outcome ? sat_inc(old_loc, 3) : sat_dec(old_loc, 3);

  // If global and local guessed differently, then update the correct one
  if (glb_prediction != loc_prediction) {
    tr_chooser[chooser_index] = outcome == glb_prediction
                                    ? sat_inc(tr_chooser[chooser_index], 2)
                                    : sat_dec(tr_chooser[chooser_index], 2);
  }

  ghistory = ((ghistory << 1) | outcome);
  tr_lht[pc & ((1 << TR_LOC_MASK_BITS) - 1)] =
      ((tr_lht[pc & ((1 << TR_LOC_MASK_BITS) - 1)] << 1) | outcome);
}

// gshare functions
void init_gshare() {
  for (size_t i = 0; i < (1 << GX_GLB_HIST_BITS); i++)
    gshare_bht[i] = WN;

  ghistory = 0;
}

uint8_t gshare_predict(uint32_t pc) {
  // get lower ghistoryBits of pc
  uint32_t bht_entries = 1 << GX_GLB_HIST_BITS;
  uint32_t pc_lower_bits = pc & (bht_entries - 1);
  uint32_t ghistory_lower_bits = ghistory & (bht_entries - 1);
  uint32_t index = pc_lower_bits ^ ghistory_lower_bits;
  switch (gshare_bht[index]) {
  case WN:
    return NOTTAKEN;
  case SN:
    return NOTTAKEN;
  case WT:
    return TAKEN;
  case ST:
    return TAKEN;
  default:
    printf("Warning: Undefined state of entry in GSHARE BHT!\n");
    return NOTTAKEN;
  }
}

void train_gshare(uint32_t pc, uint8_t outcome) {
  // get lower ghistoryBits of pc
  uint32_t bht_entries = 1 << GX_GLB_HIST_BITS;
  uint32_t pc_lower_bits = pc & (bht_entries - 1);
  uint32_t ghistory_lower_bits = ghistory & (bht_entries - 1);
  uint32_t index = pc_lower_bits ^ ghistory_lower_bits;

  // Update state of entry in bht based on outcome
  switch (gshare_bht[index]) {
  case WN:
    gshare_bht[index] = (outcome == TAKEN) ? WT : SN;
    break;
  case SN:
    gshare_bht[index] = (outcome == TAKEN) ? WN : SN;
    break;
  case WT:
    gshare_bht[index] = (outcome == TAKEN) ? ST : WN;
    break;
  case ST:
    gshare_bht[index] = (outcome == TAKEN) ? ST : WT;
    break;
  default:
    printf("Warning: Undefined state of entry in GSHARE BHT!\n");
    break;
  }

  // Update history register
  ghistory = ((ghistory << 1) | outcome);
}

void init_predictor() {
  switch (bpType) {
  case STATIC:
    break;
  case GSHARE:
    init_gshare();
    break;
  case TOURNAMENT:
    init_tournament();
    break;
  case CUSTOM:
    break;
  default:
    break;
  }
}

// Make a prediction for conditional branch instruction at PC 'pc'
// Returning TAKEN indicates a prediction of taken; returning NOTTAKEN
// indicates a prediction of not taken
//
uint32_t make_prediction(uint32_t pc, uint32_t target, uint32_t direct) {

  // Make a prediction based on the bpType
  switch (bpType) {
  case STATIC:
    return TAKEN;
  case GSHARE:
    return gshare_predict(pc);
  case TOURNAMENT:
    return tournament_predict(pc);
  case CUSTOM:
    return NOTTAKEN;
  default:
    break;
  }

  // If there is not a compatable bpType then return NOTTAKEN
  return NOTTAKEN;
}

// Train the predictor the last executed branch at PC 'pc' and with
// outcome 'outcome' (true indicates that the branch was taken, false
// indicates that the branch was not taken)
//

void train_predictor(uint32_t pc, uint32_t target, uint32_t outcome,
                     uint32_t condition, uint32_t call, uint32_t ret,
                     uint32_t direct) {
  if (condition) {
    switch (bpType) {
    case STATIC:
      return;
    case GSHARE:
      return train_gshare(pc, outcome);
    case TOURNAMENT:
      return train_tournament(pc, outcome);
    case CUSTOM:
      return;
    default:
      break;
    }
  }
}
