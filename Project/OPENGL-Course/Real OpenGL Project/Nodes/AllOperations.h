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

#include "Nodes/Operations/Op_Generate.h"
#include "Nodes/Operations/Op_Transform.h"
#include "Nodes/Operations/Op_Noise.h"
#include "Nodes/Operations/Op_Mesh.h"
#include "Nodes/Operations/Op_Selection.h"
#include "Nodes/Operations/Op_Utility.h"
#include "Nodes/Operations/Op_Erosion.h"
#include "Nodes/Operations/Op_Logic.h"
