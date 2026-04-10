#pragma once

struct CorrectionFactor
{
   const char* id;
   float tempF;
   float hum;
};

constexpr CorrectionFactor CORRECTIONS[] = {
"2003674483",  0.000,  0.000, // reference sensor
"3060770163",  0.107, -0.540,
"2978522483",  0.037, -0.601,
"2000135539", -0.061,  0.048,
"2977408371",  0.300, -0.101,
"3029116275", -0.020, -0.600,
"3045565811",  0.067, -0.140,
"3011618163", -0.124, -1.063,
"3060245875",  0.096, -1.394,
"2000135539",  0.073, -0.011,
"3008996723", -0.081, -0.579,
"337756392",   0.286, -0.627,
"246251566",   0.202, -4.932,
"290643347",   0.195, -5.611,
"290634094",  -0.038, -5.420,
// From Water Testing
"186780459",  -0.005, -1.313, // Lake SHT4X Water 1
"186772275",   0.053,  0.143, // Lake SHT4X Water 2
"350023307",  -0.034,  0.590, // Lake SHT4X Water 3
"186780455",  -0.014,  0.580, // Lake SHT4X Water 4
/* From Air Testing
"186780459",  -0.008, -1.261, // Lake SHT4X Water 1
"186772275",   0.005,  0.435, // Lake SHT4X Water 2
"350023307",  -0.021,  0.204, // Lake SHT4X Water 3
"186780455",   0.025,  0.622, // Lake SHT4X Water 4
*/

};

constexpr auto NUM_CORRECTIONS = sizeof(CORRECTIONS) / sizeof(CORRECTIONS[0]);

