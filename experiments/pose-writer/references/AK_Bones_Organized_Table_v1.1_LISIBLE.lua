-- ============================================================
-- BATMAN ARKHAM KNIGHT - ORGANIZED BONES
-- Cheat Engine 7.6 / Lua Engine
-- Version 1.1 - readable edition
--
-- 297 bones: Body, Head/Face, Arms, Legs, Belt, etc.
-- Read-only intent: creates CE records to view raw transforms.
-- Note: CE memory records can be manually edited; do not edit
--       the raw quaternion until the manipulator is ready.
--
-- BEFORE RUNNING:
-- 1. Attach Cheat Engine to BatmanAK.exe.
-- 2. Load the original AKBones scanner v0.9.
-- 3. Run AKBones.scan() and AKBones.mapAll().
-- 4. Execute this entire file in CE's Lua Engine.
--
-- Re-run AKBones.scan()/mapAll() after a level reload.
-- ============================================================

-- AK Bones - Organized Runtime Table v1.1
-- READ ONLY table builder.
-- Requirement: run AKBones.mapAll() first.

if not AKBones or not AKBones.fullSkeleton then
  error("[AK Bones] Run AKBones.mapAll() first.")
end

local BONEINFO = {
  [  0] = { fname =   1530, num = 0, name = "Bip01" },
  [  1] = { fname =   1531, num = 0, name = "Bip01_Pelvis" },
  [  2] = { fname =   1532, num = 0, name = "Bip01_Spine" },
  [  3] = { fname =   1533, num = 0, name = "Bip01_Spine1" },
  [  4] = { fname =   1534, num = 0, name = "Bip01_Spine2" },
  [  5] = { fname =   1535, num = 0, name = "Bip01_Spine3" },
  [  6] = { fname =   1540, num = 0, name = "Bip01_Neck" },
  [  7] = { fname =   1542, num = 0, name = "Bip01_Head" },
  [  8] = { fname =  55443, num = 0, name = "Bip01_HeadNub" },
  [  9] = { fname =  75356, num = 0, name = "FcFX_EyBrw_Outer_L" },
  [ 10] = { fname =  75388, num = 0, name = "FcFX_Nose_R" },
  [ 11] = { fname =  75343, num = 0, name = "FcFX_Cheek_L" },
  [ 12] = { fname =  75361, num = 0, name = "FcFX_EyLd_Bottom_R" },
  [ 13] = { fname =  75360, num = 0, name = "FcFX_EyLd_Bottom_L" },
  [ 14] = { fname =  75362, num = 0, name = "FcFX_EyLd_Top_L" },
  [ 15] = { fname =  75395, num = 0, name = "FcFX_Pinch_R" },
  [ 16] = { fname =  75394, num = 0, name = "FcFX_Pinch_L" },
  [ 17] = { fname =  75383, num = 0, name = "FcFX_Mouth_07" },
  [ 18] = { fname =  75382, num = 0, name = "FcFX_Mouth_06" },
  [ 19] = { fname =  75366, num = 0, name = "FcFX_FrHead_L" },
  [ 20] = { fname =  75341, num = 0, name = "FcFX_BotNose" },
  [ 21] = { fname =  75392, num = 0, name = "FcFX_OuterEye_R" },
  [ 22] = { fname =  75390, num = 0, name = "FcFX_OuterEye_L" },
  [ 23] = { fname =  75364, num = 0, name = "FcFX_FrHead_Cntr" },
  [ 24] = { fname =  75368, num = 0, name = "FcFX_FrHead_R" },
  [ 25] = { fname =  75372, num = 0, name = "FcFX_InsideCheek_R" },
  [ 26] = { fname =  75370, num = 0, name = "FcFX_InsideCheek_L" },
  [ 27] = { fname =  75339, num = 0, name = "FcFX_BlowCheek_R" },
  [ 28] = { fname =  75337, num = 0, name = "FcFX_BlowCheek_L" },
  [ 29] = { fname =  75377, num = 0, name = "FcFX_Mouth_01" },
  [ 30] = { fname =  75345, num = 0, name = "FcFX_Cheek_R" },
  [ 31] = { fname =  75386, num = 0, name = "FcFX_Nose_L" },
  [ 32] = { fname =  75384, num = 0, name = "FcFX_Mouth_08" },
  [ 33] = { fname =  75363, num = 0, name = "FcFX_EyLd_Top_R" },
  [ 34] = { fname =  75352, num = 0, name = "FcFX_EyBrw_Mid_L" },
  [ 35] = { fname =  75348, num = 0, name = "FcFX_EyBrw_Inner_L" },
  [ 36] = { fname =  75358, num = 0, name = "FcFX_EyBrw_Outer_R" },
  [ 37] = { fname =  75354, num = 0, name = "FcFX_EyBrw_Mid_R" },
  [ 38] = { fname =  75350, num = 0, name = "FcFX_EyBrw_Inner_R" },
  [ 39] = { fname =  75381, num = 0, name = "FcFX_Mouth_05" },
  [ 40] = { fname =  75380, num = 0, name = "FcFX_Mouth_04" },
  [ 41] = { fname =  75379, num = 0, name = "FcFX_Mouth_03" },
  [ 42] = { fname =  75378, num = 0, name = "FcFX_Mouth_02" },
  [ 43] = { fname =  75374, num = 0, name = "FcFX_Jaw" },
  [ 44] = { fname =  75396, num = 0, name = "FcFX_Tongue_01" },
  [ 45] = { fname =  75397, num = 0, name = "FcFX_Tongue_02" },
  [ 46] = { fname =  75385, num = 0, name = "FcFX_Mouth_Chin" },
  [ 47] = { fname =   1609, num = 0, name = "FcFx_Eye_L" },
  [ 48] = { fname =   1610, num = 0, name = "FcFx_Eye_R" },
  [ 49] = { fname =   1543, num = 0, name = "Bip01_L_Clavicle" },
  [ 50] = { fname =   1544, num = 0, name = "Bip01_L_UpperArm" },
  [ 51] = { fname =   1548, num = 0, name = "Bip01_L_Forearm" },
  [ 52] = { fname =   1551, num = 0, name = "Bip01_L_Hand" },
  [ 53] = { fname =   1552, num = 0, name = "Bip01_L_Finger0" },
  [ 54] = { fname =   1553, num = 0, name = "Bip01_L_Finger01" },
  [ 55] = { fname =   1554, num = 0, name = "Bip01_L_Finger02" },
  [ 56] = { fname =  55448, num = 0, name = "Bip01_L_Finger0Nub" },
  [ 57] = { fname =   1555, num = 0, name = "Bip01_L_Finger1" },
  [ 58] = { fname =   1556, num = 0, name = "Bip01_L_Finger11" },
  [ 59] = { fname =   1557, num = 0, name = "Bip01_L_Finger12" },
  [ 60] = { fname =  55449, num = 0, name = "Bip01_L_Finger1Nub" },
  [ 61] = { fname =   1558, num = 0, name = "Bip01_L_Finger2" },
  [ 62] = { fname =   1559, num = 0, name = "Bip01_L_Finger21" },
  [ 63] = { fname =   1560, num = 0, name = "Bip01_L_Finger22" },
  [ 64] = { fname =  55450, num = 0, name = "Bip01_L_Finger2Nub" },
  [ 65] = { fname =   1561, num = 0, name = "Bip01_L_Finger3" },
  [ 66] = { fname =   1562, num = 0, name = "Bip01_L_Finger31" },
  [ 67] = { fname =   1563, num = 0, name = "Bip01_L_Finger32" },
  [ 68] = { fname =  55451, num = 0, name = "Bip01_L_Finger3Nub" },
  [ 69] = { fname =   1564, num = 0, name = "Bip01_L_Finger4" },
  [ 70] = { fname =   1565, num = 0, name = "Bip01_L_Finger41" },
  [ 71] = { fname =   1566, num = 0, name = "Bip01_L_Finger42" },
  [ 72] = { fname =  55452, num = 0, name = "Bip01_L_Finger4Nub" },
  [ 73] = { fname =   1567, num = 0, name = "Gundummy02" },
  [ 74] = { fname =  76659, num = 0, name = "Flappy_Knuckle_L_Finger1" },
  [ 75] = { fname =  76660, num = 0, name = "Flappy_Knuckle_L_Finger2" },
  [ 76] = { fname =  76661, num = 0, name = "Flappy_Knuckle_L_Finger3" },
  [ 77] = { fname =  76662, num = 0, name = "Flappy_Knuckle_L_Finger4" },
  [ 78] = { fname =  51253, num = 0, name = "Batman_Gauntlet_L_Btm_Root" },
  [ 79] = { fname =  51256, num = 0, name = "Batman_Gauntlet_L_Twist" },
  [ 80] = { fname =  51255, num = 0, name = "Batman_Gauntlet_L_Screen_Root" },
  [ 81] = { fname =   1758, num = 0, name = "Bip01_Joint_Fix_L_Elbow" },
  [ 82] = { fname =   1549, num = 0, name = "Bip01_L_Foretwist" },
  [ 83] = { fname =   1550, num = 0, name = "Bip01_L_Foretwist1" },
  [ 84] = { fname =  51258, num = 0, name = "Batman_Gauntlet_L_Wrist_Root" },
  [ 85] = { fname =  51257, num = 0, name = "Batman_Gauntlet_L_Wrist_Btm" },
  [ 86] = { fname =  51259, num = 0, name = "Batman_Gauntlet_L_Wrist_Top" },
  [ 87] = { fname =  51252, num = 0, name = "Batman_Gauntlet_L_Btm_01" },
  [ 88] = { fname =  51254, num = 0, name = "Batman_Gauntlet_L_Middle_Main" },
  [ 89] = { fname =  56258, num = 0, name = "BM_L_Arm_Blade_003" },
  [ 90] = { fname =  56257, num = 0, name = "BM_L_Arm_Blade_002" },
  [ 91] = { fname =  56256, num = 0, name = "BM_L_Arm_Blade_001" },
  [ 92] = { fname = 182446, num = 0, name = "FLappy_Muscle_L_Bicep" },
  [ 93] = { fname =   1545, num = 0, name = "Bip01_LUpArmTwist" },
  [ 94] = { fname =   1546, num = 0, name = "Bip01_LUpArmTwist1" },
  [ 95] = { fname =   1547, num = 0, name = "Bip01_LUpArmTwist2" },
  [ 96] = { fname = 182460, num = 0, name = "Muscle_L_Tricep" },
  [ 97] = { fname =   1568, num = 0, name = "Bip01_R_Clavicle" },
  [ 98] = { fname =   1569, num = 0, name = "Bip01_R_UpperArm" },
  [ 99] = { fname =   1573, num = 0, name = "Bip01_R_Forearm" },
  [100] = { fname =   1576, num = 0, name = "Bip01_R_Hand" },
  [101] = { fname =   1577, num = 0, name = "Bip01_R_Finger0" },
  [102] = { fname =   1578, num = 0, name = "Bip01_R_Finger01" },
  [103] = { fname =   1579, num = 0, name = "Bip01_R_Finger02" },
  [104] = { fname =  55457, num = 0, name = "Bip01_R_Finger0Nub" },
  [105] = { fname =   1580, num = 0, name = "Bip01_R_Finger1" },
  [106] = { fname =   1581, num = 0, name = "Bip01_R_Finger11" },
  [107] = { fname =   1582, num = 0, name = "Bip01_R_Finger12" },
  [108] = { fname =  55458, num = 0, name = "Bip01_R_Finger1Nub" },
  [109] = { fname =   1583, num = 0, name = "Bip01_R_Finger2" },
  [110] = { fname =   1584, num = 0, name = "Bip01_R_Finger21" },
  [111] = { fname =   1585, num = 0, name = "Bip01_R_Finger22" },
  [112] = { fname =  55459, num = 0, name = "Bip01_R_Finger2Nub" },
  [113] = { fname =   1586, num = 0, name = "Bip01_R_Finger3" },
  [114] = { fname =   1587, num = 0, name = "Bip01_R_Finger31" },
  [115] = { fname =   1588, num = 0, name = "Bip01_R_Finger32" },
  [116] = { fname =  55460, num = 0, name = "Bip01_R_Finger3Nub" },
  [117] = { fname =   1589, num = 0, name = "Bip01_R_Finger4" },
  [118] = { fname =   1590, num = 0, name = "Bip01_R_Finger41" },
  [119] = { fname =   1591, num = 0, name = "Bip01_R_Finger42" },
  [120] = { fname =  55461, num = 0, name = "Bip01_R_Finger4Nub" },
  [121] = { fname =   1592, num = 0, name = "Gundummy" },
  [122] = { fname =  76663, num = 0, name = "Flappy_Knuckle_R_Finger1" },
  [123] = { fname =  76664, num = 0, name = "Flappy_Knuckle_R_Finger2" },
  [124] = { fname =  76665, num = 0, name = "Flappy_Knuckle_R_Finger3" },
  [125] = { fname =  76666, num = 0, name = "Flappy_Knuckle_R_Finger4" },
  [126] = { fname =  51261, num = 0, name = "Batman_Gauntlet_R_Btm_Root" },
  [127] = { fname =  51272, num = 0, name = "Batman_Gauntlet_R_Twist" },
  [128] = { fname =  51263, num = 0, name = "Batman_Gauntlet_R_Root" },
  [129] = { fname =  51265, num = 0, name = "Batman_Gauntlet_R_Screen_Bolt_Top_01" },
  [130] = { fname =  51266, num = 0, name = "Batman_Gauntlet_R_Screen_Bolt_Top_02" },
  [131] = { fname =  51267, num = 0, name = "Batman_Gauntlet_R_Screen_Bolt_Top_03" },
  [132] = { fname =  51268, num = 0, name = "Batman_Gauntlet_R_Screen_Bolt_Top_04" },
  [133] = { fname =  51269, num = 0, name = "Batman_Gauntlet_R_Screen_Bolt_Top_05" },
  [134] = { fname =  51264, num = 0, name = "Batman_Gauntlet_R_Screen_Bolt_Bottom" },
  [135] = { fname =  51276, num = 0, name = "Batman_Gauntlet_Screen_Root" },
  [136] = { fname =  51271, num = 0, name = "Batman_Gauntlet_R_Screen_R" },
  [137] = { fname =  51270, num = 0, name = "Batman_Gauntlet_R_Screen_L" },
  [138] = { fname =  55455, num = 0, name = "Bip01_Point_Gauntlet_Screen" },
  [139] = { fname =   1759, num = 0, name = "Bip01_Joint_Fix_R_Elbow" },
  [140] = { fname =   1574, num = 0, name = "Bip01_R_Foretwist" },
  [141] = { fname =   1575, num = 0, name = "Bip01_R_Foretwist1" },
  [142] = { fname =  51274, num = 0, name = "Batman_Gauntlet_R_Wrist_Root" },
  [143] = { fname =  51273, num = 0, name = "Batman_Gauntlet_R_Wrist_Btm" },
  [144] = { fname =  51275, num = 0, name = "Batman_Gauntlet_R_Wrist_Top" },
  [145] = { fname =  51260, num = 0, name = "Batman_Gauntlet_R_Btm_01" },
  [146] = { fname =  51262, num = 0, name = "Batman_Gauntlet_R_Middle_Main" },
  [147] = { fname =  56280, num = 0, name = "BM_R_Arm_Blade_001" },
  [148] = { fname =  56282, num = 0, name = "BM_R_Arm_Blade_003" },
  [149] = { fname =  56281, num = 0, name = "BM_R_Arm_Blade_002" },
  [150] = { fname =   1570, num = 0, name = "Bip01_RUpArmTwist" },
  [151] = { fname =   1571, num = 0, name = "Bip01_RUpArmTwist1" },
  [152] = { fname =   1572, num = 0, name = "Bip01_RUpArmTwist2" },
  [153] = { fname = 182425, num = 0, name = "BM_Armour_L_Top_Prnt" },
  [154] = { fname = 182424, num = 0, name = "BM_Armour_L_Top_001" },
  [155] = { fname = 182427, num = 0, name = "BM_Armour_R_Top_Prnt" },
  [156] = { fname = 182426, num = 0, name = "BM_Armour_R_Top_001" },
  [157] = { fname = 182455, num = 0, name = "Flappy_R_signWing_POS" },
  [158] = { fname = 182453, num = 0, name = "Flappy_R_signWing_AIM" },
  [159] = { fname = 182429, num = 0, name = "BM_Symbol_R_Wing_Top_001" },
  [160] = { fname = 182454, num = 1, name = "Flappy_R_signWing_Aimee" },
  [161] = { fname = 182454, num = 0, name = "Flappy_R_signWing_Aimee" },
  [162] = { fname = 182456, num = 1, name = "Flappy_R_signWing_UP" },
  [163] = { fname = 182456, num = 0, name = "Flappy_R_signWing_UP" },
  [164] = { fname = 182442, num = 1, name = "Flappy_L_signWing_Aimee" },
  [165] = { fname = 182442, num = 0, name = "Flappy_L_signWing_Aimee" },
  [166] = { fname = 182444, num = 1, name = "Flappy_L_signWing_UP" },
  [167] = { fname = 182444, num = 0, name = "Flappy_L_signWing_UP" },
  [168] = { fname = 182443, num = 0, name = "Flappy_L_signWing_POS" },
  [169] = { fname = 182441, num = 0, name = "Flappy_L_signWing_AIM" },
  [170] = { fname = 182428, num = 0, name = "BM_Symbol_L_Wing_Top_001" },
  [171] = { fname = 182447, num = 0, name = "Flappy_Muscle_L_Pec" },
  [172] = { fname = 182448, num = 0, name = "Flappy_Muscle_R_Pec" },
  [173] = { fname =   1594, num = 0, name = "Bip01_LThighTwist" },
  [174] = { fname =   1595, num = 0, name = "Bip01_LThighTwist1" },
  [175] = { fname = 164191, num = 0, name = "Flappy_L_Hamstring_Femoris_Prnt" },
  [176] = { fname = 164190, num = 0, name = "Flappy_L_Hamstring_Femoris_001" },
  [177] = { fname = 164193, num = 0, name = "Flappy_L_Hamstring_Tendinosus_Btm_Prnt" },
  [178] = { fname = 164192, num = 0, name = "Flappy_L_Hamstring_Tendinosus_Btm_001" },
  [179] = { fname = 164214, num = 0, name = "Flappy_L_Thigh_Medialis_Prnt" },
  [180] = { fname = 164213, num = 0, name = "Flappy_L_Thigh_Medialis_001" },
  [181] = { fname = 164210, num = 0, name = "Flappy_L_Thigh_Femoris_Prnt" },
  [182] = { fname = 164208, num = 0, name = "Flappy_L_Thigh_Femoris_001" },
  [183] = { fname = 164209, num = 0, name = "Flappy_L_Thigh_Femoris_Jiggle" },
  [184] = { fname = 164212, num = 0, name = "Flappy_L_Thigh_Lateralis_Prnt" },
  [185] = { fname = 164211, num = 0, name = "Flappy_L_Thigh_Lateralis_001" },
  [186] = { fname = 164216, num = 0, name = "Flappy_L_Thigh_Tensor_Prnt" },
  [187] = { fname = 164215, num = 0, name = "Flappy_L_Thigh_Tensor_001" },
  [188] = { fname = 164205, num = 0, name = "Flappy_L_Thigh_Adductor_Prnt" },
  [189] = { fname = 164204, num = 0, name = "Flappy_L_Thigh_Adductor_001" },
  [190] = { fname = 164195, num = 0, name = "Flappy_L_Hamstring_Tendinosus_Top_Prnt" },
  [191] = { fname = 164194, num = 0, name = "Flappy_L_Hamstring_Tendinosus_Top_001" },
  [192] = { fname = 164218, num = 0, name = "Flappy_L_Thigh_Upper_Jiggle_Prnt" },
  [193] = { fname = 164217, num = 0, name = "Flappy_L_Thigh_Upper_Jiggle" },
  [194] = { fname = 164207, num = 0, name = "Flappy_L_Thigh_Bk_Upper_Jiggle_Prnt" },
  [195] = { fname = 164206, num = 0, name = "Flappy_L_Thigh_Bk_Upper_Jiggle" },
  [196] = { fname =   1602, num = 0, name = "Bip01_RThighTwist" },
  [197] = { fname =   1603, num = 0, name = "Bip01_RThighTwist1" },
  [198] = { fname = 164312, num = 0, name = "Flappy_R_Thigh_Medialis_Prnt" },
  [199] = { fname = 164311, num = 0, name = "Flappy_R_Thigh_Medialis_001" },
  [200] = { fname = 164308, num = 0, name = "Flappy_R_Thigh_Femoris_Prnt" },
  [201] = { fname = 164306, num = 0, name = "Flappy_R_Thigh_Femoris_001" },
  [202] = { fname = 164307, num = 0, name = "Flappy_R_Thigh_Femoris_Jiggle" },
  [203] = { fname = 164291, num = 0, name = "Flappy_R_Hamstring_Tendinosus_Btm_Prnt" },
  [204] = { fname = 164290, num = 0, name = "Flappy_R_Hamstring_Tendinosus_Btm_001" },
  [205] = { fname = 164289, num = 0, name = "Flappy_R_Hamstring_Femoris_Prnt" },
  [206] = { fname = 164288, num = 0, name = "Flappy_R_Hamstring_Femoris_001" },
  [207] = { fname = 164310, num = 0, name = "Flappy_R_Thigh_Lateralis_Prnt" },
  [208] = { fname = 164309, num = 0, name = "Flappy_R_Thigh_Lateralis_001" },
  [209] = { fname = 164303, num = 0, name = "Flappy_R_Thigh_Adductor_Prnt" },
  [210] = { fname = 164302, num = 0, name = "Flappy_R_Thigh_Adductor_001" },
  [211] = { fname = 164314, num = 0, name = "Flappy_R_Thigh_Tensor_Prnt" },
  [212] = { fname = 164313, num = 0, name = "Flappy_R_Thigh_Tensor_001" },
  [213] = { fname = 164293, num = 0, name = "Flappy_R_Hamstring_Tendinosus_Top_Prnt" },
  [214] = { fname = 164292, num = 0, name = "Flappy_R_Hamstring_Tendinosus_Top_001" },
  [215] = { fname = 164316, num = 0, name = "Flappy_R_Thigh_Upper_Jiggle_Prnt" },
  [216] = { fname = 164315, num = 0, name = "Flappy_R_Thigh_Upper_Jiggle" },
  [217] = { fname = 164305, num = 0, name = "Flappy_R_Thigh_Bk_Upper_Jiggle_Prnt" },
  [218] = { fname = 164304, num = 0, name = "Flappy_R_Thigh_Bk_Upper_Jiggle" },
  [219] = { fname = 164142, num = 0, name = "flappy_belt_body_root" },
  [220] = { fname = 158680, num = 0, name = "bone_base_belt_body_07" },
  [221] = { fname = 158673, num = 0, name = "bone_base_belt_body_012" },
  [222] = { fname = 158681, num = 0, name = "bone_base_belt_body_08" },
  [223] = { fname = 158678, num = 0, name = "bone_base_belt_body_05" },
  [224] = { fname = 158677, num = 0, name = "bone_base_belt_body_04" },
  [225] = { fname = 158676, num = 0, name = "bone_base_belt_body_03" },
  [226] = { fname = 158675, num = 0, name = "bone_base_belt_body_02" },
  [227] = { fname = 158670, num = 0, name = "bone_base_belt_body_01" },
  [228] = { fname = 158679, num = 0, name = "bone_base_belt_body_06" },
  [229] = { fname = 158682, num = 0, name = "bone_base_belt_body_09" },
  [230] = { fname = 158671, num = 0, name = "bone_base_belt_body_010" },
  [231] = { fname = 158672, num = 0, name = "bone_base_belt_body_011" },
  [232] = { fname = 158674, num = 0, name = "bone_base_belt_body_013" },
  [233] = { fname = 133274, num = 0, name = "flappy_belt_root" },
  [234] = { fname =  56908, num = 0, name = "bone_base_belt_012" },
  [235] = { fname =  56916, num = 0, name = "bone_base_belt_08" },
  [236] = { fname =  56913, num = 0, name = "bone_base_belt_05" },
  [237] = { fname =  56912, num = 0, name = "bone_base_belt_04" },
  [238] = { fname =  56915, num = 0, name = "bone_base_belt_07" },
  [239] = { fname =  56911, num = 0, name = "bone_base_belt_03" },
  [240] = { fname =  56910, num = 0, name = "bone_base_belt_02" },
  [241] = { fname =  56905, num = 0, name = "bone_base_belt_01" },
  [242] = { fname =  56914, num = 0, name = "bone_base_belt_06" },
  [243] = { fname =  56917, num = 0, name = "bone_base_belt_09" },
  [244] = { fname =  56906, num = 0, name = "bone_base_belt_010" },
  [245] = { fname =  56907, num = 0, name = "bone_base_belt_011" },
  [246] = { fname =  56909, num = 0, name = "bone_base_belt_013" },
  [247] = { fname =   1593, num = 0, name = "Bip01_L_Thigh" },
  [248] = { fname =   1596, num = 0, name = "Bip01_L_Calf" },
  [249] = { fname =   1599, num = 0, name = "Bip01_L_Foot" },
  [250] = { fname =   1600, num = 0, name = "Bip01_L_Toe0" },
  [251] = { fname =  55454, num = 0, name = "Bip01_L_Toe0Nub" },
  [252] = { fname =  76667, num = 0, name = "Flappy_L_Boot_Tongue_01" },
  [253] = { fname =  76668, num = 0, name = "Flappy_L_Boot_Tongue_02" },
  [254] = { fname =  76669, num = 0, name = "Flappy_L_Boot_Tongue_03" },
  [255] = { fname =   1760, num = 0, name = "Bip01_Joint_Fix_L_Knee" },
  [256] = { fname =   1597, num = 0, name = "Bip01_LCalfTwist" },
  [257] = { fname =   1598, num = 0, name = "Bip01_LCalfTwist1" },
  [258] = { fname = 182445, num = 0, name = "Flappy_L_Solius_Prnt" },
  [259] = { fname = 179056, num = 0, name = "Flappy_L_Solius_001" },
  [260] = { fname = 182437, num = 0, name = "Flappy_L_Anterior_Tibialis_Prnt" },
  [261] = { fname = 179051, num = 0, name = "Flappy_L_Anterior_Tibialis_001" },
  [262] = { fname = 179053, num = 0, name = "Flappy_L_Calf_Bk_Root" },
  [263] = { fname = 179052, num = 0, name = "Flappy_L_Calf_Bk" },
  [264] = { fname = 182440, num = 0, name = "Flappy_L_Calf_Jiggle" },
  [265] = { fname = 179054, num = 0, name = "Flappy_L_Calf_L" },
  [266] = { fname = 179055, num = 0, name = "Flappy_L_Calf_R" },
  [267] = { fname =   1601, num = 0, name = "Bip01_R_Thigh" },
  [268] = { fname =   1604, num = 0, name = "Bip01_R_Calf" },
  [269] = { fname =   1607, num = 0, name = "Bip01_R_Foot" },
  [270] = { fname =   1608, num = 0, name = "Bip01_R_Toe0" },
  [271] = { fname =  55463, num = 0, name = "Bip01_R_Toe0Nub" },
  [272] = { fname =  76751, num = 0, name = "Flappy_R_Boot_Tongue_01" },
  [273] = { fname =  76752, num = 0, name = "Flappy_R_Boot_Tongue_02" },
  [274] = { fname =  76753, num = 0, name = "Flappy_R_Boot_Tongue_03" },
  [275] = { fname =   1761, num = 0, name = "Bip01_Joint_Fix_R_Knee" },
  [276] = { fname =   1605, num = 0, name = "Bip01_RCalfTwist" },
  [277] = { fname =   1606, num = 0, name = "Bip01_RCalfTwist1" },
  [278] = { fname = 182457, num = 0, name = "Flappy_R_Solius_Prnt" },
  [279] = { fname = 179062, num = 0, name = "Flappy_R_Solius_001" },
  [280] = { fname = 182449, num = 0, name = "Flappy_R_Anterior_Tibialis_Prnt" },
  [281] = { fname = 179057, num = 0, name = "Flappy_R_Anterior_Tibialis_001" },
  [282] = { fname = 179059, num = 0, name = "Flappy_R_Calf_Bk_Root" },
  [283] = { fname = 179058, num = 0, name = "Flappy_R_Calf_Bk" },
  [284] = { fname = 182452, num = 0, name = "Flappy_R_Calf_Jiggle" },
  [285] = { fname = 179060, num = 0, name = "Flappy_R_Calf_L" },
  [286] = { fname = 179061, num = 0, name = "Flappy_R_Calf_R" },
  [287] = { fname = 164296, num = 0, name = "Flappy_R_Hip_Skin_Slide_Prnt" },
  [288] = { fname = 164295, num = 0, name = "Flappy_R_Hip_Skin_Slide_001" },
  [289] = { fname = 164198, num = 0, name = "Flappy_L_Hip_Skin_Slide_Prnt" },
  [290] = { fname = 164197, num = 0, name = "Flappy_L_Hip_Skin_Slide_001" },
  [291] = { fname = 164146, num = 0, name = "Flappy_Crotch_Fix_Prnt" },
  [292] = { fname = 164145, num = 0, name = "Flappy_Crotch_Fix_001" },
  [293] = { fname = 182439, num = 0, name = "Flappy_L_Butt_Prnt" },
  [294] = { fname = 182438, num = 0, name = "Flappy_L_Butt" },
  [295] = { fname = 182451, num = 0, name = "Flappy_R_Butt_Prnt" },
  [296] = { fname = 182450, num = 0, name = "Flappy_R_Butt" },
}

local function lower(s)
  return string.lower(s or "")
end

local function has(s, p)
  return string.find(lower(s), lower(p), 1, true) ~= nil
end

local function category(name, idx)
  local n = lower(name)

  if idx >= 6 and idx <= 48 then
    if has(n,"eye_l") or has(n,"eye_r") or has(n,"eyld") or has(n,"outereye") then
      return "02 Head / Face", "02 Eyes / Eyelids"
    elseif has(n,"eybrw") or has(n,"frhead") then
      return "02 Head / Face", "03 Brows / Forehead"
    elseif has(n,"mouth") or has(n,"jaw") or has(n,"tongue") or has(n,"chin") then
      return "02 Head / Face", "04 Mouth / Jaw / Tongue"
    elseif has(n,"nose") then
      return "02 Head / Face", "05 Nose"
    elseif has(n,"cheek") or has(n,"pinch") then
      return "02 Head / Face", "06 Cheeks / Face"
    else
      return "02 Head / Face", "01 Head / Neck"
    end
  end

  if idx <= 5 then
    return "01 Body", "01 Root / Pelvis / Spine"
  elseif idx >= 49 and idx <= 96 then
    if has(n,"finger") or has(n,"knuckle") then
      return "03 Left Arm", "03 Hand / Fingers"
    elseif has(n,"gauntlet") or has(n,"arm_blade") or has(n,"gundummy") then
      return "03 Left Arm", "04 Gauntlet / Equipment"
    elseif has(n,"muscle") then
      return "03 Left Arm", "05 Muscles / Flappy"
    else
      return "03 Left Arm", "01 Arm / Twist"
    end
  elseif idx >= 97 and idx <= 152 then
    if has(n,"finger") or has(n,"knuckle") then
      return "04 Right Arm", "03 Hand / Fingers"
    elseif has(n,"gauntlet") or has(n,"arm_blade") or has(n,"gundummy") then
      return "04 Right Arm", "04 Gauntlet / Equipment"
    else
      return "04 Right Arm", "01 Arm / Twist"
    end
  elseif idx >= 153 and idx <= 172 then
    return "05 Chest / Armour", "01 Armour / Symbol / Pecs"
  elseif idx >= 173 and idx <= 195 then
    return "06 Left Leg", "01 Thigh / Muscles"
  elseif idx >= 196 and idx <= 218 then
    return "07 Right Leg", "01 Thigh / Muscles"
  elseif idx >= 219 and idx <= 246 then
    if has(n,"belt_body") then
      return "08 Utility Belt", "01 Belt Body"
    else
      return "08 Utility Belt", "02 Belt"
    end
  elseif idx >= 247 and idx <= 266 then
    if has(n,"boot") then
      return "06 Left Leg", "04 Boot"
    elseif has(n,"calf") or has(n,"solius") or has(n,"tibialis") then
      return "06 Left Leg", "03 Calf / Lower Leg"
    else
      return "06 Left Leg", "02 Main Leg / Foot"
    end
  elseif idx >= 267 and idx <= 286 then
    if has(n,"boot") then
      return "07 Right Leg", "04 Boot"
    elseif has(n,"calf") or has(n,"solius") or has(n,"tibialis") then
      return "07 Right Leg", "03 Calf / Lower Leg"
    else
      return "07 Right Leg", "02 Main Leg / Foot"
    end
  elseif idx >= 287 and idx <= 296 then
    return "09 Hips / Lower Body", "01 Skin / Butt / Crotch"
  end

  return "99 Other", "01 Unclassified"
end

local state = AKBones.fullSkeleton.state
local localAtoms = state and state.localAtoms

if not localAtoms or localAtoms == 0 then
  error("[AK Bones] LocalAtoms unavailable. Run AKBones.mapAll() again.")
end

local al = getAddressList()

for i = al.Count - 1, 0, -1 do
  local mr = al[i]
  if mr and mr.Description == "AK Bones - Batman [297]" then
    mr.destroy()
  end
end

local root = al.createMemoryRecord()
root.Description = "AK Bones - Batman [297]"
root.IsGroupHeader = true
root.Options = "[moHideChildren]"

local groups = {}

local function group(parent, key, desc)
  local fullkey = tostring(parent.ID) .. "|" .. key
  if groups[fullkey] then
    return groups[fullkey]
  end

  local mr = al.createMemoryRecord()
  mr.Description = desc
  mr.IsGroupHeader = true
  mr.Options = "[moHideChildren]"
  mr.appendToEntry(parent)
  groups[fullkey] = mr
  return mr
end

local function floatRecord(parent, desc, address)
  local mr = al.createMemoryRecord()
  mr.Description = desc
  mr.Address = string.format("%X", address)
  mr.Type = vtSingle
  mr.appendToEntry(parent)
  return mr
end

local function textInfo(parent, text)
  local mr = al.createMemoryRecord()
  mr.Description = text
  mr.IsGroupHeader = true
  mr.appendToEntry(parent)
  return mr
end

for idx = 0, 296 do
  local b = BONEINFO[idx]
  if not b then
    error(string.format("[AK Bones] Missing BONEINFO entry %d", idx))
  end

  local topName, subName = category(b.name, idx)
  local top = group(root, topName, topName)
  local sub = group(top, subName, subName)

  local bone = al.createMemoryRecord()
  bone.Description = string.format("%03d - %s", idx, b.name)
  bone.IsGroupHeader = true
  bone.Options = "[moHideChildren]"
  bone.appendToEntry(sub)

  local info = group(bone, "Info", "Info")
  textInfo(info, string.format("Runtime Index = %d", idx))
  textInfo(info, string.format("FName = %d:%d", b.fname, b.num))

  local base = localAtoms + idx * 0x20

  local pos = group(bone, "Position", "Position [Local]")
  floatRecord(pos, "X", base + 0x10)
  floatRecord(pos, "Y", base + 0x14)
  floatRecord(pos, "Z", base + 0x18)

  local rot = group(bone, "Rotation", "Rotation [Quaternion / Local]")
  floatRecord(rot, "QX", base + 0x00)
  floatRecord(rot, "QY", base + 0x04)
  floatRecord(rot, "QZ", base + 0x08)
  floatRecord(rot, "QW", base + 0x0C)

  floatRecord(bone, "Scale", base + 0x1C)
end

print("")
print("======================================================")
print(" AK Bones - Organized Runtime Table v1.1")
print("======================================================")
print(string.format("Created: 297 bones | LocalAtoms: %X", localAtoms))
print("Root: AK Bones - Batman [297]")
print("READ ONLY")
