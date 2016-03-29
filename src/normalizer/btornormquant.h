/*  Boolector: Satisfiablity Modulo Theories (SMT) solver.
 *
 *  Copyright (C) 2016 Mathias Preiner.
 *
 *  All rights reserved.
 *
 *  This file is part of Boolector.
 *  See COPYING for more information on using this software.
 */

#ifndef BTORNORMQUANT_H_INCLUDED
#define BTORNORMQUANT_H_INCLUDED

#include "btortypes.h"

BtorNode* btor_normalize_quantifiers_node (Btor* btor, BtorNode* root);

BtorNode* btor_normalize_quantifiers (Btor* btor);

#endif