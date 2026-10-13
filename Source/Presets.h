#pragma once
#include <vector>
#include <utility>

namespace oc {

struct Preset {
    const char* name;
    std::vector<std::pair<const char*, float>> values;   // parameter id -> value (unlisted = default)
};

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> P = {
        { "Default", {} },
        { "Init (neutral)", {
            {"pitch_on",0}, {"opto_on",0},
            {"mast_b1_comp",0},{"mast_b2_comp",0},{"mast_b3_comp",0},
            {"mast_b1_sat",0},{"mast_b2_sat",0},{"mast_b3_sat",0},
            {"mast_glue",0},{"mast_drive",0},{"mast_width",100},{"mast_bassmono",0} } },
        { "Vocal Tune", {
            {"pitch_on",1},{"pitch_speed",20},{"pitch_amount",1},
            {"opto_on",1},{"opto_peak",45},{"opto_gain",4},
            {"mast_b1_comp",10},{"mast_b2_comp",20},{"mast_b3_comp",15},
            {"mast_b3_sat",20},{"mast_high",1.5f},{"mast_glue",40},
            {"eq1_on",1},{"eq1_shape",3},{"eq1_freq",90},{"eq1_slope",1},
            {"eq2_on",1},{"eq2_shape",0},{"eq2_freq",300},{"eq2_gain",-2},{"eq2_q",1.2f},
            {"eq3_on",1},{"eq3_shape",0},{"eq3_freq",3500},{"eq3_gain",2},{"eq3_q",0.9f},
            {"eq4_on",1},{"eq4_shape",2},{"eq4_freq",11000},{"eq4_gain",1.5f} } },
        { "Hard Tune", {
            {"pitch_on",1},{"pitch_speed",1},{"pitch_amount",1},
            {"opto_on",1},{"opto_peak",35},{"opto_gain",3},{"mast_glue",30},
            {"eq1_on",1},{"eq1_shape",3},{"eq1_freq",80},{"eq1_slope",1} } },
        { "Mastering Glue", {
            {"pitch_on",0},{"opto_on",0},
            {"mast_b1_comp",25},{"mast_b2_comp",30},{"mast_b3_comp",25},
            {"mast_b1_sat",15},{"mast_b2_sat",10},{"mast_b3_sat",15},
            {"mast_glue",100},{"mast_glue_thr",-14},{"mast_glue_ratio",2},
            {"mast_drive",2},{"mast_width",110},{"mast_bassmono",120},
            {"eq1_on",1},{"eq1_shape",3},{"eq1_freq",30},{"eq1_slope",3} } },
        { "Warm & Wide", {
            {"pitch_on",0},{"opto_on",0},
            {"mast_b1_sat",30},{"mast_b2_sat",20},{"mast_b3_sat",10},
            {"mast_low",1.5f},{"mast_high",1.5f},{"mast_glue",70},
            {"mast_drive",3},{"mast_width",130},{"mast_bassmono",140} } },
        { "Loud & Punchy", {
            {"pitch_on",0},{"opto_on",0},
            {"mast_b1_comp",45},{"mast_b2_comp",50},{"mast_b3_comp",40},
            {"mast_b1_sat",25},{"mast_b2_sat",20},{"mast_b3_sat",20},
            {"mast_glue",100},{"mast_glue_thr",-18},{"mast_glue_ratio",3},
            {"mast_glue_atk",10},{"mast_glue_rel",120},
            {"mast_drive",4},{"mast_ceiling",-0.5f},{"mast_bassmono",120} } },
    };
    return P;
}

} // namespace oc
