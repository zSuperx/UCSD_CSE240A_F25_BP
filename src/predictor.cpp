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

#define MY_LOC_HIST_BITS 11 // Number of bits used for Local History
#define MY_LOC_MASK_BITS 10 // Number of PC bits to use to index into LHT
#define MY_GLB_HIST_BITS 14 // Number of bits used for Global History
#define MY_CHOOSER_BITS 13  // Number of bits used for chooser

#define SL 0 // local strong
#define WL 1 // local weak
#define WG 2 // global weak
#define SG 3 // global strong

// chooses between global and local
uint8_t my_chooser[1 << MY_CHOOSER_BITS];  // ((1 << MY_CHOOSER_BITS) * 2)
uint16_t my_lht[1 << MY_LOC_MASK_BITS];    // ((1 << MY_LOC_MASK_BITS) *
                                           // MY_LOC_HIST_BITS)
uint8_t my_glb_bht[1 << MY_GLB_HIST_BITS]; // ((1 << MY_GLB_HIST_BITS) * 2)
uint8_t my_loc_bht[1 << MY_LOC_HIST_BITS]; // ((1 << MY_LOC_HIST_BITS) * 3)

static const uint32_t my_size = ((1 << MY_CHOOSER_BITS) * 2) +
                                ((1 << MY_LOC_MASK_BITS) * MY_LOC_HIST_BITS) +
                                ((1 << MY_GLB_HIST_BITS) * 2) +
                                ((1 << MY_LOC_HIST_BITS) * 3);

uint64_t phr;
static_assert(65536 + 1024 >= my_size);

void init_custom() {

  size_t i;

  // Init chooser table - weakly local
  for (i = 0; i < (1 << MY_CHOOSER_BITS); i++)
    my_chooser[i] = WL;

  // Init local bht - weakly not taken
  for (i = 0; i < (1 << MY_LOC_HIST_BITS); i++)
    my_loc_bht[i] = 0b011;

  // Init global bht - weakly taken
  for (i = 0; i < (1 << MY_GLB_HIST_BITS); i++)
    my_glb_bht[i] = WT;

  // Init local history - all 0s
  for (i = 0; i < (1 << MY_LOC_MASK_BITS); i++)
    my_lht[i] = 0x0;

  // Init global history - all 0s
  ghistory = 0;
  phr = 0;
}

uint8_t custom_predict(uint32_t pc) {
  uint32_t glb_bht_index = (phr ^ pc) & ((1 << MY_GLB_HIST_BITS) - 1);
  uint32_t chooser_index = (phr ^ pc) & ((1 << MY_CHOOSER_BITS) - 1);
  switch (my_chooser[chooser_index]) {
  case SG:
  case WG: {
    // Calculate global prediction
    return my_glb_bht[glb_bht_index] > 1;
  } break;
  case WL:
  case SL: {
    // Calculate local prediction
    uint32_t loc_bht_index = my_lht[pc & ((1 << MY_LOC_MASK_BITS) - 1)] &
                             ((1 << MY_LOC_HIST_BITS) - 1);

    return my_loc_bht[loc_bht_index] > 3;
  } break;
  default:
    // printf("Warning: Undefined state of enmyy in custom CHOOSER: %0.3b "
    //        "=> %d\n",
    //        my_chooser[chooser_index], my_chooser[chooser_index]);
    return NOTTAKEN;
  }
}

void train_custom(uint32_t pc, uint32_t target, uint32_t outcome,
                  uint32_t condition, uint32_t call, uint32_t ret,
                  uint32_t direct) {
  uint32_t loc_bht_index = my_lht[pc & ((1 << MY_LOC_MASK_BITS) - 1)] &
                           ((1 << MY_LOC_HIST_BITS) - 1);
  uint32_t glb_bht_index = (phr ^ pc) & ((1 << MY_GLB_HIST_BITS) - 1);
  uint32_t chooser_index = (phr ^ pc) & ((1 << MY_CHOOSER_BITS) - 1);

  uint8_t old_glb = my_glb_bht[glb_bht_index];
  uint8_t glb_prediction = old_glb >> 1;
  uint8_t old_loc = my_loc_bht[loc_bht_index];
  uint8_t loc_prediction = old_loc >> 2;

  // Update global bht
  my_glb_bht[glb_bht_index] =
      outcome ? sat_inc(old_glb, 2) : sat_dec(old_glb, 2);

  // Update local bht
  my_loc_bht[loc_bht_index] =
      outcome ? sat_inc(old_loc, 3) : sat_dec(old_loc, 3);

  // If global and local guessed differently, then update the correct one
  if (glb_prediction != loc_prediction) {
    my_chooser[chooser_index] = outcome == glb_prediction
                                    ? sat_inc(my_chooser[chooser_index], 2)
                                    : sat_dec(my_chooser[chooser_index], 2);
  }

  uint16_t footprint = ((pc & 0b1111100000000000)) |
                       ((pc & 0b0000011111111000) >> 3) |
                       ((pc & 0b0000000000000111) << 8);
  footprint = footprint ^ (target & 0b0000000000000011) ^
              ((target & 0b0000000000111100) << 6);

  phr = (((phr << 2) ^ footprint) & ((1u << MY_GLB_HIST_BITS) - 1));

  my_lht[pc & ((1 << MY_LOC_MASK_BITS) - 1)] =
      ((my_lht[pc & ((1 << MY_LOC_MASK_BITS) - 1)] << 1) | outcome);
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
      return train_custom(pc, target, outcome, condition, call, ret, direct);
    default:
      break;
    }
  }
}
