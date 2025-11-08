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

int bpType; // Branch Prediction Type
int verbose;

//------------------------------------//
//         Utility Functions          //
//------------------------------------//

// Global branch history, free for all to use
uint64_t ghistory;

static inline uint8_t sat_inc(uint8_t v, unsigned bits) {
  uint8_t max = (uint8_t)((1u << bits) - 1u);
  return (v < max) ? (v + 1u) : max;
}

static inline uint8_t sat_dec(uint8_t v, unsigned bits) {
  return (v > 0u) ? (v - 1u) : 0u;
}

//------------------------------------//
//       Tournament Predictor         //
//------------------------------------//

#define TR_LOC_HIST_BITS 11 // Number of bits used for Local History
#define TR_LOC_MASK_BITS 11 // Number of PC bits to use to index into LHT
#define TR_GLB_HIST_BITS 13 // Number of bits used for Global History
#define TR_CHOOSER_BITS 13  // Number of bits used for chooser

#define SL 0 // local strong
#define WL 1 // local weak
#define WG 2 // global weak
#define SG 3 // global strong

// chooses between global and local
uint8_t tr_chooser[1 << TR_CHOOSER_BITS];  // ((1 << TR_CHOOSER_BITS) * 2)
uint16_t tr_lht[1 << TR_LOC_MASK_BITS];    // ((1 << TR_LOC_MASK_BITS) *
                                           // TR_LOC_HIST_BITS)
uint8_t tr_glb_bht[1 << TR_GLB_HIST_BITS]; // ((1 << TR_GLB_HIST_BITS) * 2)
uint8_t tr_loc_bht[1 << TR_LOC_HIST_BITS]; // ((1 << TR_LOC_HIST_BITS) * 3)

static_assert(65536 + 1024 >= ((1 << TR_CHOOSER_BITS) * 2) +
                                  ((1 << TR_LOC_MASK_BITS) * TR_LOC_HIST_BITS) +
                                  ((1 << TR_GLB_HIST_BITS) * 2) +
                                  ((1 << TR_LOC_HIST_BITS) * 3));

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
    // printf("Warning: Undefined state of entry in TOURNAMENT CHOOSER: %0.3b "
    //        "=> %d\n",
    //        tr_chooser[chooser_index], tr_chooser[chooser_index]);
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

//------------------------------------//
//         Custom Predictor           //
//------------------------------------//

#define SKEW_HISTORY 13

uint8_t sk_bht0[1 << SKEW_HISTORY];
uint8_t sk_bht1[1 << SKEW_HISTORY];
uint8_t sk_bht2[1 << SKEW_HISTORY];

static inline uint32_t hash(uint32_t x, uint32_t length) {
  uint32_t lsb = x & 1u;
  uint32_t msb = (x & (1u << (length - 1))) > 0;
  return (x >> 1) | ((lsb ^ msb) << (length - 1));
}

static inline uint32_t hash_inv(uint32_t x, uint32_t length) {
  uint32_t msb = (x & (1u << (length - 1))) > 0;
  uint32_t prev_msb = (x & (1u << (length - 2))) > 0;
  return (x << 1) | ((msb ^ prev_msb) & ((1u << length) - 1u));
}

void init_custom() {
  size_t i;
  for (i = 0; i < (1 << SKEW_HISTORY); i++) {
    sk_bht0[i] = WN;
    sk_bht1[i] = WN;
    sk_bht2[i] = WN;
  }

  ghistory = 0;
}

uint8_t custom_predict(uint32_t pc) {
  size_t lower_pc = pc & ((1 << SKEW_HISTORY) - 1);

  uint32_t v1 = lower_pc & ((1 << (SKEW_HISTORY / 2)) - 1);
  uint32_t v2 =
      (lower_pc >> (SKEW_HISTORY / 2)) & ((1 << (SKEW_HISTORY / 2)) - 1);

  uint32_t f0 =
      hash(v1, (SKEW_HISTORY / 2)) ^ hash_inv(v2, (SKEW_HISTORY / 2)) ^ v2;
  uint32_t f1 =
      hash(v1, (SKEW_HISTORY / 2)) ^ hash_inv(v2, (SKEW_HISTORY / 2)) ^ v1;
  uint32_t f2 =
      hash(v2, (SKEW_HISTORY / 2)) ^ hash_inv(v1, (SKEW_HISTORY / 2)) ^ v2;

  int8_t majority = (sk_bht0[f0] > 1 ? 1 : -1) + (sk_bht1[f1] > 1 ? 1 : -1) +
                    (sk_bht2[f2] > 1 ? 1 : -1);

  return majority > 0;
}

void train_custom(uint32_t pc, uint8_t outcome) {
  size_t lower_pc = pc & ((1 << SKEW_HISTORY) - 1);

  uint32_t v1 = lower_pc & ((1 << (SKEW_HISTORY / 2)) - 1);
  uint32_t v2 =
      (lower_pc >> (SKEW_HISTORY / 2)) & ((1 << (SKEW_HISTORY / 2)) - 1);

  uint32_t f0 =
      hash(v1, (SKEW_HISTORY / 2)) ^ hash_inv(v2, (SKEW_HISTORY / 2)) ^ v2;
  uint32_t f1 =
      hash(v1, (SKEW_HISTORY / 2)) ^ hash_inv(v2, (SKEW_HISTORY / 2)) ^ v1;
  uint32_t f2 =
      hash(v2, (SKEW_HISTORY / 2)) ^ hash_inv(v1, (SKEW_HISTORY / 2)) ^ v2;

  int8_t majority = (sk_bht0[f0] > 1 ? 1 : -1) + (sk_bht1[f1] > 1 ? 1 : -1) +
                    (sk_bht2[f2] > 1 ? 1 : -1);

  majority = majority > 0;

  // printf("f0: %0.2b\n"
  //        "f1: %0.2b\n"
  //        "f2: %0.2b\n"
  //        "majority = %0.2b\n\n",
  //        sk_bht0[f0], sk_bht1[f1], sk_bht2[f2], majority);

  if (majority != outcome) {
    sk_bht0[f0] = outcome ? sat_inc(sk_bht0[f0], 2) : sat_dec(sk_bht0[f0],
    2); sk_bht1[f1] = outcome ? sat_inc(sk_bht1[f1], 2) :
    sat_dec(sk_bht1[f1], 2); sk_bht2[f2] = outcome ? sat_inc(sk_bht2[f2], 2)
    : sat_dec(sk_bht2[f2], 2);
  } else {
    if (sk_bht0[f0] == outcome)
      sk_bht0[f0] = outcome ? sat_inc(sk_bht0[f0], 2) : sat_dec(sk_bht0[f0],
      2);
    if (sk_bht1[f1] == outcome)
      sk_bht1[f1] = outcome ? sat_inc(sk_bht1[f1], 2) : sat_dec(sk_bht1[f1],
      2);
    if (sk_bht2[f2] == outcome)
      sk_bht2[f2] = outcome ? sat_inc(sk_bht2[f2], 2) : sat_dec(sk_bht2[f2],
      2);
  }
}

//------------------------------------//
//         Gshare Predictor           //
//------------------------------------//

int ghistoryBits = 15; // why isn't this a macro..??
uint8_t *bht_gshare;

void init_gshare() {
  int bht_entries = 1 << ghistoryBits;
  bht_gshare = (uint8_t *)malloc(bht_entries * sizeof(uint8_t));
  int i = 0;
  for (i = 0; i < bht_entries; i++) {
    bht_gshare[i] = WN;
  }
  ghistory = 0;
}

uint8_t gshare_predict(uint32_t pc) {
  // get lower ghistoryBits of pc
  uint32_t bht_entries = 1 << ghistoryBits;
  uint32_t pc_lower_bits = pc & (bht_entries - 1);
  uint32_t ghistory_lower_bits = ghistory & (bht_entries - 1);
  uint32_t index = pc_lower_bits ^ ghistory_lower_bits;
  switch (bht_gshare[index]) {
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
  uint32_t bht_entries = 1 << ghistoryBits;
  uint32_t pc_lower_bits = pc & (bht_entries - 1);
  uint32_t ghistory_lower_bits = ghistory & (bht_entries - 1);
  uint32_t index = pc_lower_bits ^ ghistory_lower_bits;

  // Update state of entry in bht based on outcome
  switch (bht_gshare[index]) {
  case WN:
    bht_gshare[index] = (outcome == TAKEN) ? WT : SN;
    break;
  case SN:
    bht_gshare[index] = (outcome == TAKEN) ? WN : SN;
    break;
  case WT:
    bht_gshare[index] = (outcome == TAKEN) ? ST : WN;
    break;
  case ST:
    bht_gshare[index] = (outcome == TAKEN) ? ST : WT;
    break;
  default:
    printf("Warning: Undefined state of entry in GSHARE BHT!\n");
    break;
  }

  // Update history register
  ghistory = ((ghistory << 1) | outcome);
}

//------------------------------------//
//         Predictor Select           //
//------------------------------------//

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
    init_custom();
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
    return custom_predict(pc);
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
      return train_custom(pc, outcome);
    default:
      break;
    }
  }
}
