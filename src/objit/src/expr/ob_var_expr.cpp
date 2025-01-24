/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX JIT
#include "objit/expr/ob_var_expr.h"
#include "expr/ob_expr_visitor.h"

namespace oceanbase {
namespace jit {
namespace expr {

using namespace ::oceanbase::common;

//// member function definitions
//////////////////////////////////////////////////////////////////////
int ObVarExpr::accept(ObExprVisitor &v) const
{
  return v.visit(*this);
}

}  // expr
}  // jit
}  // oceanbase
