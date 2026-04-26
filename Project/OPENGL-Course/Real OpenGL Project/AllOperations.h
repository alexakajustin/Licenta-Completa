#pragma once

// =====================================================================
//  AllOperations.h — Master include for all operation registrations
// =====================================================================
//
//  Including this header triggers the REGISTER_OPERATION macros in each
//  operation file, which causes them to self-register with the 
//  OperationRegistry singleton before main() runs.
//
//  This file must be #included in exactly ONE .cpp file (typically
//  the one that creates the Application or where CustomNode.cpp is).
//

#include "Operations/Op_Generate.h"
#include "Operations/Op_Transform.h"
#include "Operations/Op_Noise.h"
#include "Operations/Op_Mesh.h"
#include "Operations/Op_Selection.h"
#include "Operations/Op_Utility.h"
#include "Operations/Op_Erosion.h"
#include "Operations/Op_Logic.h"
