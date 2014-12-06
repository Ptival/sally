grammar btor;

options {
  // C output for antlr
  language = 'C';  
}
 
@parser::includes {
  #include <string>
  #include "parser/command.h"
  #include "parser/btor/btor_state.h"
  using namespace sal2;
}

@parser::postinclude {
  #define STATE (ctx->pState)
}


@parser::context
{
  /** The sal2 part of the parser state */
  parser::btor_state* pState;
}

/** Parses the file and produces the commands */
command returns [parser::command* cmd = 0] 
  : definition* EOF { 
  	  $cmd = 0; 
    } 
  ;
  
/** Parses a single BTOR definition */
definition 
@declarations {
  std::string name;
  expr::term_ref term;
  expr::term_ref type;
  expr::bitvector bv;
}
  : // Variables 
    id=pos_integer 'var' size=pos_integer (name=symbol[name])?                  
    { STATE->add_variable(id, size, name); }
    // Constants 
  | id=pos_integer 'constd' size=pos_integer bv_constant[bv, size]                 
    { STATE->add_constant(id, size, bv); }
    // Simple binary operations 
  | id=pos_integer op=bv_binary_op size=pos_integer t1=subterm t2=subterm
    { STATE->add_term(id, op, size, t1, t2); }
  | // Extraction/Slicing
    id=pos_integer 'slice' size=pos_integer t=subterm high=pos_integer low=pos_integer
    { STATE->add_slice(id, size, t, high, low); }
  | // Conditional
    id=pos_integer 'cond' size=pos_integer t1=subterm t2=subterm t3=subterm
    { STATE->add_ite(id, size, t1, t2, t3); }
  | // Next 
    id=pos_integer 'next' size=pos_integer var_id=pos_integer t=subterm
    { STATE->add_next_variable(id, size, var_id, t); } 
  | // Root nodes 
    id=pos_integer 'root' size=pos_integer t1=subterm 
    { STATE->add_root(id, size, t1); }
  ;

/** Parse a binary operator type */
bv_binary_op returns [expr::term_op op]
  : 'xor'    { op = expr::TERM_BV_XOR;  }
  | 'sra'    { op = expr::TERM_BV_ASHR; }
  | 'concat' { op = expr::TERM_BV_CONCAT; }
  | 'eq'     { op = expr::TERM_EQ; }
  | 'and'    { op = expr::TERM_BV_AND; }
  | 'or'     { op = expr::TERM_BV_OR; }
  | 'add'    { op = expr::TERM_BV_ADD; }
  | 'ulte'   { op = expr::TERM_BV_ULEQ; }
  ;

/** A subterm */
subterm returns [expr::term_ref subterm]
  : id=integer { subterm = STATE->get_term(id); }
  ;
  
/** Parses an machine size integer */  
integer returns [int value = -1; ]
  :     p=pos_integer { value = p; }
  | '-' p=pos_integer { value = -p; }
  ;

pos_integer returns [size_t value ]
  : NUMERAL { value = STATE->token_as_int($NUMERAL); }
  ;

/** Parses an ubounded integer */
bv_constant[expr::bitvector& bv, size_t size]
@declarations {
	expr::integer bv_int;
}
  : NUMERAL { 
  	  bv_int = STATE->token_as_integer($NUMERAL, 10);
  	  bv = expr::bitvector(size, bv_int); 
  	}
  ;
  
/** Parse a symbol (name) */
symbol[std::string& id] 
  : SYMBOL { id = STATE->token_text($SYMBOL); }
  ;  
  
/** Comments (skip) */
COMMENT
  : ';' (~('\n' | '\r'))* { SKIP(); }
  ; 
  
/** Whitespace (skip) */
WHITESPACE
  : (' ' | '\t' | '\f' | '\r' | '\n')+ { SKIP(); }
  ;
  
/** Matches a symbol. */
SYMBOL
  : ALPHA (ALPHA | DIGIT | '_' | '@' | '.')*
  ;

/** Matches a letter. */
fragment
ALPHA : 'a'..'z' | 'A'..'Z';

/** Matches a numeral (sequence of digits) */
NUMERAL: DIGIT+;

/** Matches a digit */
fragment 
DIGIT : '0'..'9';  
 