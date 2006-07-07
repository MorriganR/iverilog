/*
 * Copyright (c) 1999-2005 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#ifdef HAVE_CVS_IDENT
#ident "$Id: elab_expr.cc,v 1.106 2006/07/07 04:06:37 steve Exp $"
#endif

# include "config.h"
# include "compiler.h"

# include  "pform.h"
# include  "netlist.h"
# include  "netmisc.h"
# include  "util.h"

NetExpr* PExpr::elaborate_expr(Design*des, NetScope*, int, bool) const
{
      cerr << get_line() << ": internal error: I do not know how to elaborate"
	   << " expression. " << endl;
      cerr << get_line() << ":               : Expression is: " << *this
	   << endl;
      des->errors += 1;
      return 0;
}

/*
 * Elaborate binary expressions. This involves elaborating the left
 * and right sides, and creating one of a variety of different NetExpr
 * types.
 */
NetEBinary* PEBinary::elaborate_expr(Design*des, NetScope*scope,
				     int expr_wid, bool) const
{
      assert(left_);
      assert(right_);

      NetExpr*lp = left_->elaborate_expr(des, scope, expr_wid, false);
      NetExpr*rp = right_->elaborate_expr(des, scope, expr_wid, false);
      if ((lp == 0) || (rp == 0)) {
	    delete lp;
	    delete rp;
	    return 0;
      }


	/* If either expression can be evaluated ahead of time, then
	   do so. This can prove helpful later. */
      { NetExpr*tmp;
        tmp = lp->eval_tree();
	if (tmp) {
	      delete lp;
	      lp = tmp;
	}

	tmp = rp->eval_tree();
	if (tmp) {
	      delete rp;
	      rp = tmp;
	}
      }

      NetEBinary*tmp = elaborate_expr_base_(des, lp, rp);
      return tmp;
}

/*
 * This is common elaboration of the operator. It presumes that the
 * operands are elaborated as necessary, and all I need to do is make
 * the correct NetEBinary object and connect the parameters.
 */
NetEBinary* PEBinary::elaborate_expr_base_(Design*des,
					   NetExpr*lp, NetExpr*rp) const
{
      bool flag;
      NetEBinary*tmp;

      switch (op_) {
	  default:
	    tmp = new NetEBinary(op_, lp, rp);
	    tmp->set_line(*this);
	    break;

	  case 'a':
	  case 'o':
	    tmp = new NetEBLogic(op_, lp, rp);
	    tmp->set_line(*this);
	    break;

	  case '*':
	    tmp = new NetEBMult(op_, lp, rp);
	    tmp->set_line(*this);
	    break;

	  case '/':
	  case '%':
	    tmp = new NetEBDiv(op_, lp, rp);
	    tmp->set_line(*this);
	    break;

	  case 'l': // <<
	  case 'r': // >>
	  case 'R': // >>>
	    tmp = new NetEBShift(op_, lp, rp);
	    tmp->set_line(*this);
	    break;

	  case '^':
	  case '&':
	  case '|':
	  case 'O': // NOR (~|)
	  case 'A': // NAND (~&)
	  case 'X':
	    tmp = new NetEBBits(op_, lp, rp);
	    tmp->set_line(*this);
	    break;

	  case '+':
	  case '-':
	    tmp = new NetEBAdd(op_, lp, rp);
	    tmp->set_line(*this);
	    break;

	  case 'E': /* === */
	  case 'N': /* !== */
	    if (lp->expr_type() == IVL_VT_REAL
		|| rp->expr_type() == IVL_VT_REAL) {
		  cerr << get_line() << ": error: Case equality may not "
		       << "have real operands." << endl;
		  return 0;
	    }
	      /* Fall through... */
	  case 'e': /* == */
	  case 'n': /* != */
	    if (dynamic_cast<NetEConst*>(rp)
		&& (lp->expr_width() > rp->expr_width()))
		  rp->set_width(lp->expr_width());

	    if (dynamic_cast<NetEConst*>(lp)
		&& (lp->expr_width() < rp->expr_width()))
		  lp->set_width(rp->expr_width());

	      /* from here, handle this like other compares. */
	  case 'L': /* <= */
	  case 'G': /* >= */
	  case '<':
	  case '>':
	    tmp = new NetEBComp(op_, lp, rp);
	    tmp->set_line(*this);
	    flag = tmp->set_width(1);
	    if (flag == false) {
		  cerr << get_line() << ": internal error: "
			"expression bit width of comparison != 1." << endl;
		  des->errors += 1;
	    }
	    break;
      }

      return tmp;
}

/*
 * Given a call to a system function, generate the proper expression
 * nodes to represent the call in the netlist. Since we don't support
 * size_tf functions, make assumptions about widths based on some
 * known function names.
 */
NetExpr* PECallFunction::elaborate_sfunc_(Design*des, NetScope*scope) const
{

	/* Catch the special case that the system function is the
	   $signed function. This function is special, in that it does
	   not lead to executable code but takes the single parameter
	   and makes it into a signed expression. No bits are changed,
	   it just changes the interpretation. */
      if (strcmp(path_.peek_name(0), "$signed") == 0) {
	    if ((parms_.count() != 1) || (parms_[0] == 0)) {
		  cerr << get_line() << ": error: The $signed() function "
		       << "takes exactly one(1) argument." << endl;
		  des->errors += 1;
		  return 0;
	    }

	    PExpr*expr = parms_[0];
	    NetExpr*sub = expr->elaborate_expr(des, scope, -1, true);
	    sub->cast_signed(true);
	    return sub;
      }
      /* add $unsigned to match $signed */
      if (strcmp(path_.peek_name(0), "$unsigned") == 0) {
	    if ((parms_.count() != 1) || (parms_[0] == 0)) {
		  cerr << get_line() << ": error: The $unsigned() function "
		       << "takes exactly one(1) argument." << endl;
		  des->errors += 1;
		  return 0;
	    }

	    PExpr*expr = parms_[0];
	    NetExpr*sub = expr->elaborate_expr(des, scope, -1, true);
	    sub->cast_signed(false);
	    return sub;
      }

	/* Interpret the internal $sizeof system function to return
	   the bit width of the sub-expression. The value of the
	   sub-expression is not used, so the expression itself can be
	   deleted. */
      if ((strcmp(path_.peek_name(0), "$sizeof") == 0)
	  || (strcmp(path_.peek_name(0), "$bits") == 0)) {
	    if ((parms_.count() != 1) || (parms_[0] == 0)) {
		  cerr << get_line() << ": error: The $bits() function "
		       << "takes exactly one(1) argument." << endl;
		  des->errors += 1;
		  return 0;
	    }

	    if (strcmp(path_.peek_name(0), "$sizeof") == 0)
		  cerr << get_line() << ": warning: $sizeof is deprecated."
		       << " Use $bits() instead." << endl;

	    PExpr*expr = parms_[0];
	    NetExpr*sub = expr->elaborate_expr(des, scope, -1, true);
	    verinum val (sub->expr_width(), 8*sizeof(unsigned));
	    delete sub;

	    sub = new NetEConst(val);
	    sub->set_line(*this);

	    return sub;
      }

	/* Interpret the internal $is_signed system function to return
	   a single bit flag -- 1 if the expression is signed, 0
	   otherwise. The subexpression is elaborated but not
	   evaluated. */
      if (strcmp(path_.peek_name(0), "$is_signed") == 0) {
	    if ((parms_.count() != 1) || (parms_[0] == 0)) {
		  cerr << get_line() << ": error: The $is_signed() function "
		       << "takes exactly one(1) argument." << endl;
		  des->errors += 1;
		  return 0;
	    }

	    PExpr*expr = parms_[0];
	    NetExpr*sub = expr->elaborate_expr(des, scope, -1, true);

	    verinum val (sub->has_sign()? verinum::V1 : verinum::V0, 1);
	    delete sub;

	    sub = new NetEConst(val);
	    sub->set_line(*this);

	    return sub;
      }

	/* Get the return type of the system function by looking it up
	   in the sfunc_table. */
      const struct sfunc_return_type*sfunc_info
	    = lookup_sys_func(path_.peek_name(0));

      ivl_variable_type_t sfunc_type = sfunc_info->type;
      unsigned wid = sfunc_info->wid;


	/* How many parameters are there? The Verilog language allows
	   empty parameters in certain contexts, so the parser will
	   allow things like func(1,,3). It will also cause func() to
	   be interpreted as a single empty parameter.

	   Functions cannot really take empty parameters, but the
	   case ``func()'' is the same as no parameters at all. So
	   catch that special case here. */
      unsigned nparms = parms_.count();
      if ((nparms == 1) && (parms_[0] == 0))
	    nparms = 0;

      NetESFunc*fun = new NetESFunc(path_.peek_name(0), sfunc_type,
				    wid, nparms);
      if (sfunc_info->signed_flag)
	    fun->cast_signed(true);

	/* Now run through the expected parameters. If we find that
	   there are missing parameters, print an error message.

	   While we're at it, try to evaluate the function parameter
	   expression as much as possible, and use the reduced
	   expression if one is created. */

      unsigned missing_parms = 0;
      for (unsigned idx = 0 ;  idx < nparms ;  idx += 1) {
	    PExpr*expr = parms_[idx];
	    if (expr) {
		  NetExpr*tmp1 = expr->elaborate_expr(des, scope, -1, true);
		  if (NetExpr*tmp2 = tmp1->eval_tree()) {
			delete tmp1;
			fun->parm(idx, tmp2);
		  } else {
			fun->parm(idx, tmp1);
		  }

	    } else {
		  missing_parms += 1;
		  fun->parm(idx, 0);
	    }
      }

      if (missing_parms > 0) {
	    cerr << get_line() << ": error: The function "
		 << path_.peek_name(0)
		 << " has been called with empty parameters." << endl;
	    cerr << get_line() << ":      : Verilog doesn't allow "
		 << "passing empty parameters to functions." << endl;
	    des->errors += 1;
      }

      return fun;
}

NetExpr* PECallFunction::elaborate_expr(Design*des, NetScope*scope,
					int expr_wid, bool) const
{
      if (path_.peek_name(0)[0] == '$')
	    return elaborate_sfunc_(des, scope);

      NetFuncDef*def = des->find_function(scope, path_);
      if (def == 0) {
	    cerr << get_line() << ": error: No function " << path_ <<
		  " in this context (" << scope->name() << ")." << endl;
	    des->errors += 1;
	    return 0;
      }
      assert(def);

      NetScope*dscope = def->scope();
      assert(dscope);

      if (! check_call_matches_definition_(des, dscope))
	    return 0;

      unsigned parms_count = parms_.count();
      if ((parms_count == 1) && (parms_[0] == 0))
	    parms_count = 0;



      svector<NetExpr*> parms (parms_count);

	/* Elaborate the input expressions for the function. This is
	   done in the scope of the function call, and not the scope
	   of the function being called. The scope of the called
	   function is elaborated when the definition is elaborated. */

      unsigned missing_parms = 0;
      for (unsigned idx = 0 ;  idx < parms.count() ;  idx += 1) {
	    PExpr*tmp = parms_[idx];
	    if (tmp) {
		  parms[idx] = tmp->elaborate_expr(des, scope, -1, false);

	    } else {
		  missing_parms += 1;
		  parms[idx] = 0;
	    }
      }

      if (missing_parms > 0) {
	    cerr << get_line() << ": error: The function " << path_
		 << " has been called with empty parameters." << endl;
	    cerr << get_line() << ":      : Verilog doesn't allow "
		 << "passing empty parameters to functions." << endl;
	    des->errors += 1;
      }


	/* Look for the return value signal for the called
	   function. This return value is a magic signal in the scope
	   of the function, that has the name of the function. The
	   function code assigns to this signal to return a value.

	   dscope, in this case, is the scope of the function, so the
	   return value is the name within that scope. */

      if (NetNet*res = dscope->find_signal(dscope->basename())) {
	    NetESignal*eres = new NetESignal(res);
	    NetEUFunc*func = new NetEUFunc(dscope, eres, parms);
	    return func;
      }

      cerr << get_line() << ": internal error: Unable to locate "
	    "function return value for " << path_
	   << " in " << def->name() << "." << endl;
      des->errors += 1;
      return 0;
}


NetExpr* PEConcat::elaborate_expr(Design*des, NetScope*scope,
				  int expr_wid, bool) const
{
      NetExpr* repeat = 0;

	/* If there is a repeat expression, then evaluate the constant
	   value and set the repeat count. */
      if (repeat_) {
	    NetExpr*tmp = elab_and_eval(des, scope, repeat_, -1);
	    assert(tmp);
	    NetEConst*rep = dynamic_cast<NetEConst*>(tmp);

	    if (rep == 0) {
		  cerr << get_line() << ": error: "
			"concatenation repeat expression cannot be evaluated."
		       << endl;
		  cerr << get_line() << ":      : The expression is: "
		       << *tmp << endl;
		  des->errors += 1;
	    }

	    repeat = rep;
      }

	/* Make the empty concat expression. */
      NetEConcat*tmp = new NetEConcat(parms_.count(), repeat);
      tmp->set_line(*this);

      unsigned wid_sum = 0;

	/* Elaborate all the parameters and attach them to the concat node. */
      for (unsigned idx = 0 ;  idx < parms_.count() ;  idx += 1) {
	    if (parms_[idx] == 0) {
		  cerr << get_line() << ": error: Missing expression "
		       << (idx+1) << " of concatenation list." << endl;
		  des->errors += 1;
		  continue;
	    }

	    assert(parms_[idx]);
	    NetExpr*ex = elab_and_eval(des, scope, parms_[idx], -1);
	    if (ex == 0) continue;

	    ex->set_line(*parms_[idx]);

	    if (! ex->has_width()) {
		  cerr << ex->get_line() << ": error: operand of "
		       << "concatenation has indefinite width: "
		       << *ex << endl;
		  des->errors += 1;
	    }

	    wid_sum += ex->expr_width();
	    tmp->set(idx, ex);
      }

      tmp->set_width(wid_sum * tmp->repeat());

      return tmp;
}

NetExpr* PEFNumber::elaborate_expr(Design*des, NetScope*scope, int, bool) const
{
      NetECReal*tmp = new NetECReal(*value_);
      tmp->set_line(*this);
      return tmp;
}

/*
 * Elaborate an identifier in an expression. The identifier can be a
 * parameter name, a signal name or a memory name. It can also be a
 * scope name (Return a NetEScope) but only certain callers can use
 * scope names. However, we still support it here.
 *
 * Function names are not handled here, they are detected by the
 * parser and are elaborated by PECallFunction.
 *
 * The signal name may be escaped, but that affects nothing here.
 */
NetExpr* PEIdent::elaborate_expr(Design*des, NetScope*scope,
				 int expr_wid, bool sys_task_arg) const
{
      assert(scope);

      NetNet*       net = 0;
      NetMemory*    mem = 0;
      const NetExpr*par = 0;
      NetEvent*     eve = 0;

      const NetExpr*ex1, *ex2;

      NetScope*found_in = symbol_search(des, scope, path_,
					net, mem, par, eve,
					ex1, ex2);

	// If the identifier name is a parameter name, then return
	// a reference to the parameter expression.
      if (par != 0)
	    return elaborate_expr_param(des, scope, par, found_in, ex1, ex2);


	// If the identifier names a signal (a register or wire)
	// then create a NetESignal node to handle it.
      if (net != 0)
	    return elaborate_expr_net(des, scope, net, found_in);


	// If the identifier names a memory, then this is a
	// memory reference and I must generate a NetEMemory
	// object to handle it.
      if (mem != 0) {

	    if (idx_.empty()) {

		  if (msb_ || lsb_) {
			cerr << get_line() << ": error: "
			     << "Part select of a memory: "
			     << mem->name() << endl;
			des->errors += 1;
		  }

		    // If this memory is an argument to a system task,
		    // then it is OK for it to not have an index.
		  if (sys_task_arg) {
			NetEMemory*node = new NetEMemory(mem);
			node->set_line(*this);
			return node;
		  }

		    // If it is not a simple system task argument,
		    // this a missing index is an error.
		  cerr << get_line() << ": error: memory " << mem->name()
		       << " needs an index in this context." << endl;
		  des->errors += 1;
		  return 0;
	    }

	    if (idx_.size() != mem->dimensions()) {
		  cerr << get_line() << ": error: " << idx_.size()
		       << " indices do not properly address a "
		       << mem->dimensions() << "-dimension memory/array."
		       << endl;
		  des->errors += 1;
		  return 0;
	    }

	      // XXXX For now, only support single word index.
	    assert(idx_.size() == 1);
	    PExpr*addr = idx_[0];
	    assert(addr);

	    NetExpr*i = addr->elaborate_expr(des, scope, -1, false);
	    if (i == 0) {
		  cerr << get_line() << ": error: Unable to elaborate "
			"index expression `" << *addr << "'" << endl;
		  des->errors += 1;
		  return 0;
	    }

	    NetEMemory*node = new NetEMemory(mem, i);
	    node->set_line(*this);

	    if (msb_ == 0 && lsb_ == 0)
		  return node;

	    assert(msb_ && lsb_);

	    assert(sel_ != SEL_NONE);

	    if (sel_ == SEL_PART) {

		  NetExpr*le = elab_and_eval(des, scope, lsb_, -1);
		  NetExpr*me = elab_and_eval(des, scope, msb_, -1);

		  NetEConst*lec = dynamic_cast<NetEConst*>(le);
		  NetEConst*mec = dynamic_cast<NetEConst*>(me);

		  if (lec == 0 || mec == 0) {
			cerr << get_line() << ": error: Part select "
			     << "expressions must be constant." << endl;
			des->errors += 1;
			delete le;
			delete me;
			return node;
		  }

		  verinum wedv = mec->value() - lec->value();
		  unsigned wid = wedv.as_long() + 1;

		  NetESelect*se = new NetESelect(node, le, wid);

		  delete me;
		  return se;
	    }

	    assert(sel_ == SEL_IDX_UP || sel_ == SEL_IDX_DO);

	    NetExpr*wid_ex = elab_and_eval(des, scope, lsb_, -1);
	    NetEConst*wid_ec = dynamic_cast<NetEConst*> (wid_ex);
	    if (wid_ec == 0) {
		  cerr << lsb_->get_line() << ": error: "
		       << "Second expression of indexed part select "
		       << "most be constant." << endl;
		  des->errors += 1;
		  return node;
	    }

	    unsigned wid = wid_ec->value().as_ulong();

	    NetExpr*idx_ex = elab_and_eval(des, scope, msb_, -1);
	    if (idx_ex == 0) {
		  return 0;
	    }

	    if (sel_ == SEL_IDX_DO && wid > 1) {
		  idx_ex = make_add_expr(idx_ex, 1-(long)wid);
	    }


	      /* Wrap the param expression with a part select. */
	    NetESelect*se = new NetESelect(node, idx_ex, wid);
	    se->set_line(*this);
	    return se;
      }

	// If the identifier is a named event.
	// is a variable reference.
      if (eve != 0) {
	    NetEEvent*tmp = new NetEEvent(eve);
	    tmp->set_line(*this);
	    return tmp;
      }

	// Hmm... maybe this is a genvar? This is only possible while
	// processing generate blocks, but then the genvar_tmp will be
	// set in the scope.
      if (path_.component_count() == 1
	  && scope->genvar_tmp.str()
	  && strcmp(path_.peek_name(0), scope->genvar_tmp) == 0) {
	    verinum val (scope->genvar_tmp_val);
	    NetEConst*tmp = new NetEConst(val);
	    tmp->set_line(*this);
	    return tmp;
      }

	// Finally, if this is a scope name, then return that. Look
	// first to see if this is a name of a local scope. Failing
	// that, search globally for a hierarchical name.
      if ((path_.peek_name(1) == 0))
	    if (NetScope*nsc = scope->child(path_.peek_name(0))) {
		  NetEScope*tmp = new NetEScope(nsc);
		  tmp->set_line(*this);
		  return tmp;
	    }

	// Try full hierarchical scope name.
      if (NetScope*nsc = des->find_scope(path_)) {
	    NetEScope*tmp = new NetEScope(nsc);
	    tmp->set_line(*this);
	    return tmp;
      }

	// Try relative scope name.
      if (NetScope*nsc = des->find_scope(scope, path_)) {
	    NetEScope*tmp = new NetEScope(nsc);
	    tmp->set_line(*this);
	    return tmp;
      }

	// I cannot interpret this identifier. Error message.
      cerr << get_line() << ": error: Unable to bind wire/reg/memory "
	    "`" << path_ << "' in `" << scope->name() << "'" << endl;
      des->errors += 1;
      return 0;
}

/*
 * Handle the case that the identifier is a parameter reference. The
 * parameter expression has already been located for us (as the par
 * argument) so we just need to process the sub-expression.
 */
NetExpr* PEIdent::elaborate_expr_param(Design*des,
				       NetScope*scope,
				       const NetExpr*par,
				       NetScope*found_in,
				       const NetExpr*par_msb,
				       const NetExpr*par_lsb) const
{
      NetExpr*tmp;

      tmp = par->dup_expr();

      if (sel_ == SEL_PART) {
	    assert(msb_ && lsb_);
	    assert(idx_.empty());

	      /* If the identifier has a part select, we support
		 it by pulling the right bits out and making a
		 sized unsigned constant. This code assumes the
		 lsb of a parameter is 0 and the msb is the
		 width of the parameter. */

	    verinum*lsn = lsb_->eval_const(des, scope);
	    verinum*msn = msb_->eval_const(des, scope);
	    if ((lsn == 0) || (msn == 0)) {
		  cerr << get_line() << ": error: "
			"Part select expressions must be "
			"constant expressions." << endl;
		  des->errors += 1;
		  return 0;
	    }

	    long lsb = lsn->as_long();
	    long msb = msn->as_long();
	    if ((lsb < 0) || (msb < lsb)) {
		  cerr << get_line() << ": error: invalid part "
		       << "select: " << path_
		       << "["<<msb<<":"<<lsb<<"]" << endl;
		  des->errors += 1;
		  return 0;
	    }
	    unsigned long ulsb=lsb;
	    unsigned long umsb=msb;

	    NetEConst*le = dynamic_cast<NetEConst*>(tmp);
	    assert(le);

	    verinum result (verinum::V0, msb-lsb+1, true);
	    verinum exl = le->value();

	      /* Pull the bits from the parameter, one at a
		 time. If the bit is within the range, simply
		 copy it to the result. If the bit is outside
		 the range, we sign extend signed unsized
		 numbers, zero extend unsigned unsigned numbers,
		 and X extend sized numbers. */
	    for (unsigned long idx = ulsb ;  idx <= umsb ;  idx += 1) {
		  if (idx < exl.len())
			result.set(idx-lsb, exl.get(idx));
		  else if (exl.is_string())
			result.set(idx-lsb, verinum::V0);
		  else if (exl.has_len())
			result.set(idx-lsb, verinum::Vx);
		  else if (exl.has_sign())
			result.set(idx-lsb, exl.get(exl.len()-1));
		  else
			result.set(idx-lsb, verinum::V0);
	    }

	      /* If the input is a string, and the part select
		 is working on byte boundaries, then the result
		 can be made into a string. */
	    if (exl.is_string()
		&& (lsb%8 == 0)
		&& (result.len()%8 == 0))
		  result = verinum(result.as_string());

	    delete tmp;
	    tmp = new NetEConst(result);

      } else if (sel_ == SEL_IDX_UP || sel_ == SEL_IDX_DO) {
	    assert(msb_);
	    assert(lsb_);
	    assert(idx_.empty());

	      /* Get and evaluate the width of the index
		 select. This must be constant. */
	    NetExpr*wid_ex = elab_and_eval(des, scope, lsb_, -1);
	    NetEConst*wid_ec = dynamic_cast<NetEConst*> (wid_ex);
	    if (wid_ec == 0) {
		  cerr << lsb_->get_line() << ": error: "
		       << "Second expression of indexed part select "
		       << "most be constant." << endl;
		  des->errors += 1;
		  return 0;
	    }

	    unsigned wid = wid_ec->value().as_ulong();

	    NetExpr*idx_ex = elab_and_eval(des, scope, msb_, -1);
	    if (idx_ex == 0) {
		  return 0;
	    }

	    if (sel_ == SEL_IDX_DO && wid > 1) {
		  idx_ex = make_add_expr(idx_ex, 1-(long)wid);
	    }


	      /* Wrap the param expression with a part select. */
	    tmp = new NetESelect(tmp, idx_ex, wid);


      } else if (!idx_.empty()) {
	    assert(!msb_);
	    assert(!lsb_);
	    assert(idx_.size() == 1);
	    assert(sel_ == SEL_NONE);

	      /* Handle the case where a parameter has a bit
		 select attached to it. Generate a NetESelect
		 object to select the bit as desired. */
	    NetExpr*mtmp = idx_[0]->elaborate_expr(des, scope, -1,false);
	    if (! dynamic_cast<NetEConst*>(mtmp)) {
		  NetExpr*re = mtmp->eval_tree();
		  if (re) {
			delete mtmp;
			mtmp = re;
		  }
	    }

	      /* Let's first try to get constant values for both
		 the parameter and the bit select. If they are
		 both constant, then evaluate the bit select and
		 return instead a single-bit constant. */

	    NetEConst*le = dynamic_cast<NetEConst*>(tmp);
	    NetEConst*re = dynamic_cast<NetEConst*>(mtmp);
	    if (le && re) {

		    /* Argument and bit select are constant. Calculate
		       the final result. */
		  verinum lv = le->value();
		  verinum rv = re->value();
		  verinum::V rb = verinum::Vx;

		  long ridx = rv.as_long();
		  if ((ridx >= 0) && ((unsigned long) ridx < lv.len())) {
			rb = lv[ridx];

		  } else if ((ridx >= 0) && (!lv.has_len())) {
			if (lv.has_sign())
			      rb = lv[lv.len()-1];
			else
			      rb = verinum::V0;
		  }

		  NetEConst*re = new NetEConst(verinum(rb, 1));
		  delete tmp;
		  delete mtmp;
		  tmp = re;

	    } else {

		  const NetEConst*par_me =dynamic_cast<const NetEConst*>(par_msb);
		  const NetEConst*par_le =dynamic_cast<const NetEConst*>(par_lsb);

		  assert(par_me || !par_msb);
		  assert(par_le || !par_lsb);
		  assert(par_me || !par_le);

		  if (par_me) {
			long par_mv = par_me->value().as_long();
			long par_lv = par_le->value().as_long();
			if (par_mv >= par_lv) {
			      mtmp = par_lv
				    ? make_add_expr(mtmp, 0-par_lv)
				    : mtmp;
			} else {
			      if (par_lv != 0)
				    mtmp = make_add_expr(mtmp, 0-par_mv);
			      mtmp = make_sub_expr(par_lv-par_mv, mtmp);
			}
		  }

		    /* The value is constant, but the bit select
		       expression is not. Elaborate a NetESelect to
		       evaluate the select at run-time. */

		  NetESelect*stmp = new NetESelect(tmp, mtmp, 1);
		  tmp->set_line(*this);
		  tmp = stmp;
	    }

      } else {
	      /* No bit or part select. Make the constant into a
		 NetEConstParam if possible. */
	    NetEConst*ctmp = dynamic_cast<NetEConst*>(tmp);
	    if (ctmp != 0) {
		  perm_string name
			= lex_strings.make(path_.peek_tail_name());
		  NetEConstParam*ptmp
			= new NetEConstParam(found_in, name, ctmp->value());
		  delete tmp;
		  tmp = ptmp;
	    }
      }

      tmp->set_line(*this);
      return tmp;
}

/*
 * Handle part selects of NetNet identifiers.
 */
NetExpr* PEIdent::elaborate_expr_net_part_(Design*des, NetScope*scope,
					   NetNet*net, NetScope*found_in)const
{
      assert(lsb_ != 0);
      assert(msb_ != 0);
      assert(idx_.empty());

      verinum*lsn = lsb_->eval_const(des, scope);
      verinum*msn = msb_->eval_const(des, scope);
      if ((lsn == 0) || (msn == 0)) {
	    cerr << get_line() << ": error: "
		  "Part select expressions must be "
		  "constant expressions." << endl;
	    des->errors += 1;
	    return 0;
      }

      assert(lsn);
      assert(msn);

	/* The indices of part selects are signed integers, so allow
	   negative values. However, the width that they represent is
	   unsigned. Remember that any order is possible,
	   i.e., [1:0], [-4:6], etc. */

      long lsv = lsn->as_long();
      long msv = msn->as_long();
      unsigned long wid = 1 + ((msv>lsv)? (msv-lsv) : (lsv-msv));
      if (wid > net->vector_width()) {
	    cerr << get_line() << ": error: part select ["
		 << msv << ":" << lsv << "] out of range." << endl;
	    des->errors += 1;
	    delete lsn;
	    delete msn;
	    return 0;
      }
      assert(wid <= net->vector_width());

      if (net->sb_to_idx(msv) < net->sb_to_idx(lsv)) {
	    cerr << get_line() << ": error: part select ["
		 << msv << ":" << lsv << "] out of order." << endl;
	    des->errors += 1;
	    delete lsn;
	    delete msn;
	    return 0;
      }


      if (net->sb_to_idx(msv) >= net->vector_width()) {
	    cerr << get_line() << ": error: part select ["
		 << msv << ":" << lsv << "] out of range." << endl;
	    des->errors += 1;
	    delete lsn;
	    delete msn;
	    return 0;
      }

      NetESignal*tmp = new NetESignal(net);
      tmp->set_line(*this);

	// If the part select convers exactly the entire
	// vector, then do not bother with it. Return the
	// signal itself.
      if (net->sb_to_idx(lsv) == 0 && wid == net->vector_width())
	    return tmp;

      NetExpr*ex = new NetEConst(verinum(net->sb_to_idx(lsv)));
      NetESelect*ss = new NetESelect(tmp, ex, wid);
      ss->set_line(*this);

      return ss;
}

/*
 * Part select indexed up, i.e. net[<m> +: <l>]
 */
NetExpr* PEIdent::elaborate_expr_net_idx_up_(Design*des, NetScope*scope,
					   NetNet*net, NetScope*found_in)const
{
      assert(lsb_ != 0);
      assert(msb_ != 0);
      assert(idx_.empty());

      NetESignal*sig = new NetESignal(net);
      sig->set_line(*this);

      NetExpr*base = elab_and_eval(des, scope, msb_, -1);

      NetExpr*wid_e = elab_and_eval(des, scope, lsb_, -1);
      NetEConst*wid_c = dynamic_cast<NetEConst*> (wid_e);
      if (wid_c == 0) {
	    cerr << get_line() << ": error: Width of indexed part select "
		 << "must be constant." << endl;
	    cerr << get_line() << ":      : Width expression is: "
		 << *wid_e << endl;
	    des->errors += 1;
	    return 0;
      }

      assert(wid_c != 0);
      unsigned long wid = wid_c->value().as_ulong();

	// Handle the special case that the base is constant as
	// well. In this case it can be converted to a conventional
	// part select.
      if (NetEConst*base_c = dynamic_cast<NetEConst*> (base)) {
	    long lsv = base_c->value().as_long();

	      // If the part select convers exactly the entire
	      // vector, then do not bother with it. Return the
	      // signal itself.
	    if (net->sb_to_idx(lsv) == 0 && wid == net->vector_width())
		  return sig;
      }

      NetESelect*ss = new NetESelect(sig, base, wid);
      ss->set_line(*this);

      if (debug_elaborate) {
	    cerr << get_line() << ": debug: Elaborate part "
		 << "select base="<< *base << ", wid="<< wid << endl;
      }

      return ss;
}

/*
 * Part select up, i.e. net[<m> +: <l>]
 */
NetExpr* PEIdent::elaborate_expr_net_idx_do_(Design*des, NetScope*scope,
					   NetNet*net, NetScope*found_in)const
{
      assert(lsb_ != 0);
      assert(msb_ != 0);
      assert(idx_.empty());

      NetESignal*sig = new NetESignal(net);
      sig->set_line(*this);

      NetExpr*base = elab_and_eval(des, scope, msb_, -1);

      NetExpr*wid_e = elab_and_eval(des, scope, lsb_, -1);
      NetEConst*wid_c = dynamic_cast<NetEConst*> (wid_e);
      if (wid_c == 0) {
	    cerr << get_line() << ": error: Width of indexed part select "
		 << "must be constant." << endl;
	    cerr << get_line() << ":      : Width expression is: "
		 << *wid_e << endl;
	    des->errors += 1;
	    return 0;
      }

      assert(wid_c != 0);
      long wid = wid_c->value().as_long();

	// Handle the special case that the base is constant as
	// well. In this case it can be converted to a conventional
	// part select.
      if (NetEConst*base_c = dynamic_cast<NetEConst*> (base)) {
	    long lsv = base_c->value().as_long();

	      // If the part select convers exactly the entire
	      // vector, then do not bother with it. Return the
	      // signal itself.
	    if (net->sb_to_idx(lsv) == (wid-1) && wid == net->vector_width())
		  return sig;
      }

      NetExpr*base_adjusted = wid > 1? make_add_expr(base,1-wid) : base;
      NetESelect*ss = new NetESelect(sig, base_adjusted, wid);
      ss->set_line(*this);

      if (debug_elaborate) {
	    cerr << get_line() << ": debug: Elaborate part "
		 << "select base="<< *base << ", wid="<< wid << endl;
      }

      return ss;
}

NetExpr* PEIdent::elaborate_expr_net_bit_(Design*des, NetScope*scope,
					  NetNet*net, NetScope*found_in) const
{
      assert(msb_ == 0);
      assert(lsb_ == 0);
      assert(idx_.size() == 1);

	// If the bit select is constant, then treat it similar
	// to the part select, so that I save the effort of
	// making a mux part in the netlist.
      if (verinum*msn = idx_[0]->eval_const(des, scope)) {
	    long msv = msn->as_long();
	    unsigned idx = net->sb_to_idx(msv);

	    if (idx >= net->vector_width()) {
		    /* The bit select is out of range of the
		       vector. This is legal, but returns a
		       constant 1'bx value. */
		  verinum x (verinum::Vx);
		  NetEConst*tmp = new NetEConst(x);
		  tmp->set_line(*this);

		  cerr << get_line() << ": warning: Bit select ["
		       << msv << "] out of range of vector "
		       << net->name() << "[" << net->msb()
		       << ":" << net->lsb() << "]." << endl;
		  cerr << get_line() << ":        : Replacing "
		       << "expression with a constant 1'bx." << endl;
		  delete msn;
		  return tmp;
	    }

	    NetESignal*tmp = new NetESignal(net);
	    tmp->set_line(*this);
	      // If the vector is only one bit, we are done. The
	      // bit select will return the scaler itself.
	    if (net->vector_width() == 1)
		  return tmp;

	      // Make an expression out of the index
	    NetEConst*idx_c = new NetEConst(verinum(idx));
	    idx_c->set_line(*net);

	      // Make a bit select with the canonical index
	    NetESelect*res = new NetESelect(tmp, idx_c, 1);
	    res->set_line(*net);

	    return res;
      }

      NetESignal*node = new NetESignal(net);

	// Non-constant bit select? punt and make a subsignal
	// device to mux the bit in the net. This is a fairly
	// complicated task because we need to generate
	// expressions to convert calculated bit select
	// values to canonical values that are used internally.
      NetExpr*ex = idx_[0]->elaborate_expr(des, scope, -1, false);

      if (net->msb() < net->lsb()) {
	    ex = make_sub_expr(net->lsb(), ex);
      } else {
	    ex = make_add_expr(ex, - net->lsb());
      }

      NetESelect*ss = new NetESelect(node, ex, 1);
      ss->set_line(*this);
      return ss;
}

NetExpr* PEIdent::elaborate_expr_net(Design*des, NetScope*scope,
				     NetNet*net, NetScope*found_in) const
{
	// If this is a part select of a signal, then make a new
	// temporary signal that is connected to just the
	// selected bits. The lsb_ and msb_ expressions are from
	// the foo[msb:lsb] expression in the original.
      if (sel_ == SEL_PART)
	    return elaborate_expr_net_part_(des, scope, net, found_in);

      if (sel_ == SEL_IDX_UP)
	    return elaborate_expr_net_idx_up_(des, scope, net, found_in);

      if (sel_ == SEL_IDX_DO)
	    return elaborate_expr_net_idx_do_(des, scope, net, found_in);

      if (!idx_.empty())
	    return elaborate_expr_net_bit_(des, scope, net, found_in);

	// It's not anything else, so this must be a simple identifier
	// expression with no part or bit select. Return the signal
	// itself as the expression.
      assert(sel_ == SEL_NONE);
      assert(msb_ == 0);
      assert(lsb_ == 0);
      assert(idx_.empty());

      NetESignal*node = new NetESignal(net);
      node->set_line(*this);
      return node;
}

NetEConst* PENumber::elaborate_expr(Design*des, NetScope*,
				    int expr_width, bool) const
{
      assert(value_);
      verinum tvalue = *value_;

	// If the expr_width is >0, then the context is requesting a
	// specific size (for example this is part of the r-values of
	// an assignment) so we pad to the desired width and ignore
	// the self-determined size.
      if (expr_width > 0) {
	    tvalue = pad_to_width(tvalue, expr_width);
      }

      NetEConst*tmp = new NetEConst(tvalue);
      tmp->set_line(*this);
      return tmp;
}

NetEConst* PEString::elaborate_expr(Design*des, NetScope*,
				    int expr_width, bool) const
{
      NetEConst*tmp = new NetEConst(value());
      tmp->set_line(*this);
      return tmp;
}

static bool test_ternary_operand_compat(ivl_variable_type_t l,
					ivl_variable_type_t r)
{
      if (l == IVL_VT_LOGIC && r == IVL_VT_BOOL)
	    return true;
      if (l == IVL_VT_BOOL && r == IVL_VT_LOGIC)
	    return true;
      if (l == r)
	    return true;

      return false;
}

/*
 * Elaborate the Ternary operator. I know that the expressions were
 * parsed so I can presume that they exist, and call elaboration
 * methods. If any elaboration fails, then give up and return 0.
 */
NetETernary*PETernary::elaborate_expr(Design*des, NetScope*scope,
				      int expr_wid, bool) const
{
      assert(expr_);
      assert(tru_);
      assert(fal_);

      NetExpr*con = expr_->elaborate_expr(des, scope, -1, false);
      if (con == 0)
	    return 0;

      NetExpr*tru = tru_->elaborate_expr(des, scope, expr_wid, false);
      if (tru == 0) {
	    delete con;
	    return 0;
      }

      NetExpr*fal = fal_->elaborate_expr(des, scope, expr_wid, false);
      if (fal == 0) {
	    delete con;
	    delete tru;
	    return 0;
      }

      if (! test_ternary_operand_compat(tru->expr_type(), fal->expr_type())) {
	    cerr << get_line() << ": error: Data types "
		 << tru->expr_type() << " and "
		 << fal->expr_type() << " of ternary"
		 << " do not match." << endl;
	    des->errors += 1;
	    return 0;
      }

      NetETernary*res = new NetETernary(con, tru, fal);
      res->set_line(*this);
      return res;
}

NetExpr* PEUnary::elaborate_expr(Design*des, NetScope*scope,
				 int expr_wid, bool) const
{
      NetExpr*ip = expr_->elaborate_expr(des, scope, expr_wid, false);
      if (ip == 0) return 0;

      /* Should we evaluate expressions ahead of time,
       * just like in PEBinary::elaborate_expr() ?
       */

      NetExpr*tmp;
      switch (op_) {
	  default:
	    tmp = new NetEUnary(op_, ip);
	    tmp->set_line(*this);
	    break;

	  case '-':
	    if (NetEConst*ipc = dynamic_cast<NetEConst*>(ip)) {

		  verinum val = ipc->value();
		  if (expr_wid > 0)
			val = pad_to_width(val, expr_wid);

		    /* When taking the - of a number, turn it into a
		       signed expression and extend it one bit to
		       accommodate a possible sign bit. */
		  verinum zero (verinum::V0, val.len()+1, val.has_len());
		  verinum nval = zero - val;

		  if (val.has_len())
			nval = verinum(nval, val.len());
		  nval.has_sign(true);
		  tmp = new NetEConst(nval);
		  tmp->set_line(*this);
		  delete ip;

	    } else if (NetECReal*ipc = dynamic_cast<NetECReal*>(ip)) {

		    /* When taking the - of a real, fold this into the
		       constant value. */
		  verireal val = - ipc->value();
		  tmp = new NetECReal(val);
		  tmp->set_line( *ip );
		  delete ip;

	    } else {
		  tmp = new NetEUnary(op_, ip);
		  tmp->set_line(*this);
	    }
	    break;

	  case '+':
	    tmp = ip;
	    break;

	  case '!': // Logical NOT
	      /* If the operand to unary ! is a constant, then I can
		 evaluate this expression here and return a logical
		 constant in its place. */
	    if (NetEConst*ipc = dynamic_cast<NetEConst*>(ip)) {
		  verinum val = ipc->value();
		  unsigned v1 = 0;
		  unsigned vx = 0;
		  for (unsigned idx = 0 ;  idx < val.len() ;  idx += 1)
			switch (val[idx]) {
			    case verinum::V0:
			      break;
			    case verinum::V1:
			      v1 += 1;
			      break;
			    default:
			      vx += 1;
			      break;
			}

		  verinum::V res;
		  if (v1 > 0)
			res = verinum::V0;
		  else if (vx > 0)
			res = verinum::Vx;
		  else
			res = verinum::V1;

		  verinum vres (res, 1, true);
		  tmp = new NetEConst(vres);
		  tmp->set_line(*this);
		  delete ip;
	    } else {
		  tmp = new NetEUReduce(op_, ip);
		  tmp->set_line(*this);
	    }
	    break;

	  case '&': // Reduction AND
	  case '|': // Reduction OR
	  case '^': // Reduction XOR
	  case 'A': // Reduction NAND (~&)
	  case 'N': // Reduction NOR (~|)
	  case 'X': // Reduction NXOR (~^)
	    tmp = new NetEUReduce(op_, ip);
	    tmp->set_line(*this);
	    break;

	  case '~':
	    tmp = new NetEUBits(op_, ip);
	    tmp->set_line(*this);
	    break;
      }

      return tmp;
}

/*
 * $Log: elab_expr.cc,v $
 * Revision 1.106  2006/07/07 04:06:37  steve
 *  Fix context determined with of constants.
 *
 * Revision 1.105  2006/06/02 04:48:49  steve
 *  Make elaborate_expr methods aware of the width that the context
 *  requires of it. In the process, fix sizing of the width of unary
 *  minus is context determined sizes.
 *
 * Revision 1.104  2006/06/01 03:54:51  steve
 *  Fix broken subtraction of small constants.
 *
 * Revision 1.103  2006/04/12 05:05:03  steve
 *  Use elab_and_eval to evaluate genvar expressions.
 *
 * Revision 1.102  2006/02/02 02:43:57  steve
 *  Allow part selects of memory words in l-values.
 *
 * Revision 1.101  2005/11/27 05:56:20  steve
 *  Handle bit select of parameter with ranges.
 *
 * Revision 1.100  2005/11/14 22:11:52  steve
 *  Fix compile warning.
 *
 * Revision 1.99  2005/11/10 13:28:11  steve
 *  Reorganize signal part select handling, and add support for
 *  indexed part selects.
 *
 *  Expand expression constant propagation to eliminate extra
 *  sums in certain cases.
 *
 * Revision 1.98  2005/10/04 04:09:25  steve
 *  Add support for indexed select attached to parameters.
 *
 * Revision 1.97  2005/09/19 21:45:35  steve
 *  Spelling patches from Larry.
 *
 * Revision 1.96  2005/09/14 02:53:13  steve
 *  Support bool expressions and compares handle them optimally.
 *
 * Revision 1.95  2005/09/01 04:10:47  steve
 *  Check operand types for compatibility.
 *
 * Revision 1.94  2005/07/11 16:56:50  steve
 *  Remove NetVariable and ivl_variable_t structures.
 *
 * Revision 1.93  2005/01/24 05:28:30  steve
 *  Remove the NetEBitSel and combine all bit/part select
 *  behavior into the NetESelect node and IVL_EX_SELECT
 *  ivl_target expression type.
 *
 * Revision 1.92  2004/12/11 02:31:25  steve
 *  Rework of internals to carry vectors through nexus instead
 *  of single bits. Make the ivl, tgt-vvp and vvp initial changes
 *  down this path.
 *
 * Revision 1.91  2004/10/04 01:10:52  steve
 *  Clean up spurious trailing white space.
 *
 * Revision 1.90  2004/08/28 15:42:11  steve
 *  Add support for $unsigned.
 *
 * Revision 1.89  2004/08/26 03:52:07  steve
 *  Add the $is_signed function.
 *
 * Revision 1.88  2004/06/17 16:06:18  steve
 *  Help system function signedness survive elaboration.
 *
 * Revision 1.87  2004/06/04 23:34:15  steve
 *  Special case for unary - of real literal.
 *
 * Revision 1.86  2004/05/31 23:34:36  steve
 *  Rewire/generalize parsing an elaboration of
 *  function return values to allow for better
 *  speed and more type support.
 *
 * Revision 1.85  2004/03/09 04:29:42  steve
 *  Separate out the lookup_sys_func table, for eventual
 *  support for function type tables.
 *
 *  Remove ipal compile flags.
 *
 * Revision 1.84  2004/02/20 06:22:56  steve
 *  parameter keys are per_strings.
 *
 * Revision 1.83  2004/01/21 04:57:40  steve
 *  Generate error when missing concatenation operands.
 *
 * Revision 1.82  2003/10/09 16:52:52  steve
 *  Put parameter name in NetEConstParam, not scope.
 *
 * Revision 1.81  2003/09/19 03:30:05  steve
 *  Fix name search in elab_lval.
 *
 * Revision 1.80  2003/06/24 01:38:02  steve
 *  Various warnings fixed.
 *
 * Revision 1.79  2003/06/18 03:55:18  steve
 *  Add arithmetic shift operators.
 *
 * Revision 1.78  2003/06/10 04:29:57  steve
 *  PR735: bit select indices are signed constants.
 *
 * Revision 1.77  2003/05/30 02:55:32  steve
 *  Support parameters in real expressions and
 *  as real expressions, and fix multiply and
 *  divide with real results.
 *
 * Revision 1.76  2003/04/22 04:48:29  steve
 *  Support event names as expressions elements.
 *
 * Revision 1.75  2003/04/19 04:19:38  steve
 *  Set line number for ternary expressions.
 *
 * Revision 1.74  2003/04/02 04:25:26  steve
 *  Fix xz extension of constants.
 *
 * Revision 1.73  2003/03/25 03:00:04  steve
 *  Scope names can be relative.
 *
 * Revision 1.72  2003/03/15 04:46:28  steve
 *  Better organize the NetESFunc return type guesses.
 *
 * Revision 1.71  2003/03/10 23:40:53  steve
 *  Keep parameter constants for the ivl_target API.
 *
 * Revision 1.70  2003/03/07 02:44:34  steve
 *  Implement $realtobits.
 *
 * Revision 1.69  2003/01/27 05:09:17  steve
 *  Spelling fixes.
 *
 * Revision 1.68  2003/01/26 21:15:58  steve
 *  Rework expression parsing and elaboration to
 *  accommodate real/realtime values and expressions.
 *
 * Revision 1.67  2002/12/21 00:55:57  steve
 *  The $time system task returns the integer time
 *  scaled to the local units. Change the internal
 *  implementation of vpiSystemTime the $time functions
 *  to properly account for this. Also add $simtime
 *  to get the simulation time.
 *
 * Revision 1.66  2002/09/21 21:28:18  steve
 *  Allow constant bit selects out of range.
 *
 * Revision 1.65  2002/09/18 04:08:45  steve
 *  Spelling errors.
 *
 * Revision 1.64  2002/09/12 15:49:43  steve
 *  Add support for binary nand operator.
 *
 * Revision 1.63  2002/08/19 02:39:16  steve
 *  Support parameters with defined ranges.
 *
 * Revision 1.62  2002/08/12 01:34:58  steve
 *  conditional ident string using autoconfig.
 *
 * Revision 1.61  2002/06/14 21:38:41  steve
 *  Fix expression width for repeat concatenations.
 *
 * Revision 1.60  2002/05/24 00:44:54  steve
 *  Add support for $bits (SystemVerilog)
 *
 * Revision 1.59  2002/05/06 02:30:27  steve
 *  Allow parameters in concatenation of widths are defined.
 *
 * Revision 1.58  2002/05/05 21:11:49  steve
 *  Put off evaluation of concatenation repeat expresions
 *  until after parameters are defined. This allows parms
 *  to be used in repeat expresions.
 *
 *  Add the builtin $signed system function.
 *
 * Revision 1.57  2002/04/27 05:03:46  steve
 *  Preserve stringiness string part select and concatenation.
 *
 * Revision 1.56  2002/04/27 02:38:04  steve
 *  Support selecting bits from parameters.
 *
 * Revision 1.55  2002/04/25 05:04:31  steve
 *  Evaluate constant bit select of constants.
 *
 * Revision 1.54  2002/04/14 21:16:48  steve
 *  Evaluate logical not at elaboration time.
 *
 * Revision 1.53  2002/04/14 03:55:25  steve
 *  Precalculate unary - if possible.
 *
 * Revision 1.52  2002/04/13 02:33:17  steve
 *  Detect missing indices to memories (PR#421)
 *
 * Revision 1.51  2002/03/09 02:10:22  steve
 *  Add the NetUserFunc netlist node.
 *
 * Revision 1.50  2002/01/28 00:52:41  steve
 *  Add support for bit select of parameters.
 *  This leads to a NetESelect node and the
 *  vvp code generator to support that.
 *
 * Revision 1.49  2002/01/11 05:25:45  steve
 *  The stime system function is 32bits.
 */

