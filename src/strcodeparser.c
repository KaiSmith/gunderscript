/**
 * strcodeparser.c
 * (C) 2013 Christian Gunderman
 * Modified by:
 * Author Email: gundermanc@gmail.com
 * Modifier Email:
 *
 * Description:
 * Implements the straight code parser for the compiler.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <limits.h>
#include "strcodeparser.h"
#include "langkeywords.h"
#include "lexer.h"

/* preprocessor definitions: */
#define COMPILER_NO_PREV        -1


/* TODO: make STK type auto enlarge and remove */
static const int initialOpStkDepth = 100;
/* max number of digits in a number value */
static const int numValMaxDigits = 50;
/* number of items to add to the op stack on resize */
static const int opStkBlockSize = 12;

/**
 * Gets the OP code associated with an operation from its string representation.
 * operator: the operator to get the OPCode from.
 * len: the number of characters in length that operator is.
 * returns: the OPCode, or -1 if the operator is unrecognized.
 */
OpCode operator_to_opcode(char * operator, size_t len) {
  if(tokens_equal(operator, len, LANG_OP_ADD, LANG_OP_ADD_LEN)) {
    return OP_ADD;
  } else if(tokens_equal(operator, len, LANG_OP_SUB, LANG_OP_SUB_LEN)) {
    return OP_SUB;
  } else if(tokens_equal(operator, len, LANG_OP_MUL, LANG_OP_MUL_LEN)) {
    return OP_MUL;
  } else if(tokens_equal(operator, len, LANG_OP_DIV, LANG_OP_DIV_LEN)) {
    return OP_DIV;
  } 

  /* unknown operator */
  return -1;
}

/**
 * Writes all operators from the provided stack to the output buffer in the 
 * Compiler instance, making allowances for specific behaviors for function
 * tokens, variable references, and assignments.
 * c: an instance of Compiler.
 * opStk: the stack of operator strings.
 * opLenStk: the stack of operator string lengths, in longs.
 * parenthExpected: tells whether or not the calling function is
 * expecting a parenthesis. If it is and one is not encountered, the
 * function sets c->err = COMPILERERR_UNMATCHED_PARENTH and returns false.
 * returns: true if successful, and false if an unmatched parenthesis is
 * encountered.
 */
bool write_operators_from_stack(Compiler * c, TypeStk * opStk, 
				       Stk * opLenStk, bool parenthExpected,
				       bool popParenth) {
  bool topOperator = true;
  DSValue value;
  char * token = NULL;
  size_t len = 0;
  OpCode opCode;
  VarType type;

  /* while items remain */
  while(typestk_size(opStk) > 0) {
    printf("TYPESTK SIZE: %i\n", typestk_size(opStk));
    /* get token string */
    typestk_pop(opStk, &token, sizeof(char*), &type);

    /* get token length */
    stk_pop(opLenStk, &value);
    len = value.longVal;

    /* if there is an open parenthesis...  */
    if(tokens_equal(LANG_OPARENTH, LANG_OPARENTH_LEN, token, len)) {
      printf("OPARENTHDSFLSKFJLSF\n");
      printf("TYPESTK SIZE: %i\n", typestk_size(opStk));
      /* if top of stack is a parenthesis, it is a mismatch! */
      if(!parenthExpected) {
	printf("UMP 1;\n");
	c->err = COMPILERERR_UNMATCHED_PARENTH;
	return false;
      } else if(popParenth) {
	printf("POPPING OPARENTH:\n");
	typestk_peek(opStk, &token, sizeof(char*), &type);
	stk_peek(opLenStk, &value);
	len = value.longVal;

	/* there is a function name before the open parenthesis,
	 * this is a function call:
	 */
	if(type == LEXERTYPE_KEYVAR) {
	  int callbackIndex = -1;
	  typestk_pop(opStk, &token, sizeof(char*), &type);
	  stk_pop(opLenStk, &value);

	  /* check if the function name is a provided function */
	  callbackIndex = vm_callback_index(c->vm, token, len);
	  if(callbackIndex == -1) {

	    /* the function is not a native function. check script functions */
	    if(ht_get_raw_key(c->functionHT, token, len, &value)) {
	      CompilerFunc * funcDef = value.pointerVal;
	      /* function exists, lets write the OPCodes */
	      sb_append_char(c->outBuffer, OP_CALL_B);
	      /* TODO: error check number of arguments */
	      sb_append_char(c->outBuffer, funcDef->numArgs);
	      sb_append_char(c->outBuffer, 1); /* Number of args ...TODO */
	      printf("Function Index: %i\n", funcDef->index);
	      sb_append_str(c->outBuffer, &funcDef->index, sizeof(int));
	      printf("OP_CALL_PTR_N: %s\n", token);
	    } else {
	      printf("Undefined function: %s\n", token);
	      c->err = COMPILERERR_UNDEFINED_FUNCTION;
	      return false;
	    }
	  }

	  /* function exists, lets write the OPCodes */
	  sb_append_char(c->outBuffer, OP_CALL_PTR_N);
	  /* TODO: make support mutiple arguments */
	  sb_append_char(c->outBuffer, 1);
	  sb_append_str(c->outBuffer, &callbackIndex, sizeof(int));
	  printf("OP_CALL_PTR_N: %s\n", token);
	  
	}
	return true;
      } else {
	/* Re-push the parenthesis:*/
	typestk_push(opStk, &token, sizeof(char*), type);
	stk_push_long(opLenStk, len);

	printf("CEASING WITHOUT POPPING:\n");
	printf("TYPESTK SIZE: %i\n", typestk_size(opStk));
	return true;
      }
    }

    /* handle OPERATORS and FUNCTIONS differently */
    if(type == LEXERTYPE_OPERATOR) {
      printf("POP: writing operator from OPSTK %s\n", token);
      printf("     writing operator from OPSTK LEN %i\n", len);

      /* if there is an '=' on the stack, this is an assignment,
       * the next token on the stack should be a KEYVAR type that
       * contains a variable name.
       */
      if(tokens_equal(LANG_OP_ASSIGN, LANG_OP_ASSIGN_LEN, token, len)) {

	/* get variable name string */
	if(!typestk_pop(opStk, &token, sizeof(char*), &type)
	     || type != LEXERTYPE_KEYVAR) {
	  printf("MALFORMED TOKEN: %s\n", token);
	  c->err = COMPILERERR_MALFORMED_ASSIGNMENT;
	  return false;
	}

	/* get variable name length */
	stk_pop(opLenStk, &value);
	len = value.longVal;

	assert(stk_size(c->symTableStk) > 0);

	/* check that the variable was previously declared */
	if(!ht_get_raw_key(symtblstk_peek(c), token, len, &value)) {
	  printf("UNDEFINED VAR: %s\n", token);
	  c->err = COMPILERERR_UNDEFINED_VARIABLE;
	  return false;
	}

	/* write the variable data OPCodes
	 * Moves the last value from the OP stack in the VM to the variable
	 * storage slot in the frame stack. */
	/* TODO: need to add ability to search LOWER frames for variables */
	sb_append_char(c->outBuffer, OP_VAR_STOR);
	sb_append_char(c->outBuffer, 0 /* this val should chng with depth */);
	sb_append_char(c->outBuffer, value.intVal);
	printf("ASSIGNED VALUE TO VAR: %s\n", token);
      } else {

	opCode = operator_to_opcode(token, len);

	/* check for invalid operators */
	if(opCode == -1) {

	  c->err = COMPILERERR_UNKNOWN_OPERATOR;
	  return false;
	}

	/* write operator OP code to output buffer */
	sb_append_char(c->outBuffer, opCode);
      }
    } else {
      /* Not an operator, must be a KEYVAR, and therefore, a variable read op */
      assert(stk_size(c->symTableStk) > 0);

      /* check that the variable was previously declared */
      if(!ht_get_raw_key(symtblstk_peek(c), token, len, &value)) {
	printf("Symbol table frame: %i\n", stk_size(c->symTableStk));
	printf("Undefined Var: %s %i\n", token, len);
	c->err = COMPILERERR_UNDEFINED_VARIABLE;
	return false;
      }

      /* write the variable data read OPCodes
       * Moves the last value from the specified depth and slot of the frame
       * stack in the VM to the VM OP stack.
       */
      /* TODO: need to add ability to search LOWER frames for variables */
      printf("Reading Variable\n");
      sb_append_char(c->outBuffer, OP_VAR_PUSH);
      sb_append_char(c->outBuffer, 0 /* this val should chng with depth */);
      sb_append_char(c->outBuffer, value.intVal);
    }

    /* we're no longer at the top of the stack */
    topOperator = false;
  }

  /* Is open parenthensis is expected SOMEWHERE in this sequence? If true,
   * and we reach this point, we never found one. Throw an error!
   */
  if(!parenthExpected) {
    return true;
  } else {
    printf("UMP 2;");
    /*** TODO: potentially catches false error cases */
    c->err = COMPILERERR_UNMATCHED_PARENTH;
    return false;
  }
}

static bool parse_number(Compiler * c, LexerType prevValType, 
			 char * token, size_t len) {
  /* Reads a number, converts to double and writes it to output as so:
   * OP_NUM_PUSH [value as a double]
   */
  char rawValue[numValMaxDigits];
  double value = 0;

  /* get double representation of number
     /* TODO: length check, and double overflow check */
  strncpy(rawValue, token, len);
  value = atof(rawValue);

  printf("OUTPUT: %f\n", value);

  /* write number to output */
  sb_append_char(c->outBuffer, OP_NUM_PUSH);
  sb_append_str(c->outBuffer, (char*)(&value), sizeof(double));

  /* check for invalid types: */
  if(prevValType != COMPILER_NO_PREV
     && prevValType != LEXERTYPE_PARENTHESIS
     && prevValType != LEXERTYPE_OPERATOR) {
    c->err = COMPILERERR_UNEXPECTED_TOKEN;
    return false;
  }

  return true;
}

/**
 * Handles straight code:
 * This is a modified version of Djikstra's "Shunting Yard Algorithm" for
 * conversion to postfix notation. Code is converted to postfix and written to
 * the output buffer as a series of OP codes all in one step.
 * c: an instance of Compiler.
 * l: an instance of lexer that will provide the tokens.
 * returns: true if success, and false if errors occurred. If error occurred,
 * c->err is set.
 */
/* TODO: return false for every sb_append* function upon failure */
bool parse_straight_code(Compiler * c, Lexer * l) {
  LexerType type;
  LexerType prevValType = COMPILER_NO_PREV;
  char * token;
  size_t len;

  /* allocate stacks for operators and their lengths, a.k.a. 
   * the "side track in shunting yard" 
   */
  TypeStk * opStk = typestk_new(initialOpStkDepth, opStkBlockSize);
  Stk * opLenStk = stk_new(initialOpStkDepth);
  if(opStk == NULL || opLenStk == NULL) {
    c->err = COMPILERERR_ALLOC_FAILED;
    return false;
  }

  token = lexer_current_token(l, &type, &len);
  printf("First straight code token: %s\n", token);

  printf("FIRST: %s\n", token);

  /* straight code token parse loop */
  do {
    bool popAfterNextVal = true;

    /* output values, push operators to stack with precedence so that they can
     * be postfixed for execution by the VM
     */
    switch(type) {
    case LEXERTYPE_BRACKETS:
      if(prevValType != COMPILER_NO_PREV
	 && prevValType != LEXERTYPE_ENDSTATEMENT) {
	c->err = COMPILERERR_EXPECTED_ENDSTATEMENT;
	return false;
      }
      return true; 

    case LEXERTYPE_KEYVAR: {
      /* KEYVAR HANDLER:
       * Handles all word tokens. These can be variables or function names
       */
      /* push variable or function name to operator stack */
      typestk_push(opStk, &token, sizeof(char*), type);
      stk_push_long(opLenStk, len);
      printf("KEYVAR pushed to OPSTK: %s\n", token);
      printf("KEYVAR pushed to OPSTK Len: %i\n", len);
      break;
    }

    case LEXERTYPE_NUMBER: {
      if(!parse_number(c, prevValType, token, len)) {
	return false;
      }

      break;
    }

    case LEXERTYPE_STRING: {
      /* Reads a string token and writes it raw to the output as so:
       * OP_STR_PUSH [strlen as 1 byte value] [string]
       */
      char outLen = len;

      /* string length byte has max value of CHAR_MAX. */
      if(len >= CHAR_MAX) {
	c->err = COMPILERERR_STRING_TOO_LONG;
	return false;
      }

      printf("Output string: %s\n", token);

      /* write output */
      sb_append_char(c->outBuffer, OP_STR_PUSH);
      sb_append_char(c->outBuffer, outLen);
      sb_append_str(c->outBuffer, token, len);

      /* check for invalid types: */
      if(prevValType != COMPILER_NO_PREV
	 && prevValType != LEXERTYPE_PARENTHESIS
	 && prevValType != LEXERTYPE_OPERATOR) {
	c->err = COMPILERERR_UNEXPECTED_TOKEN;
	return false;
      }
      break;
    }

    case LEXERTYPE_PARENTHESIS: {
      printf("PARENTH: %s\n", token);
      printf("PARENTH LEN: %i\n", len);
      if(tokens_equal(LANG_OPARENTH, LANG_OPARENTH_LEN, token, len)) {
	typestk_push(opStk, &token, sizeof(char*), type);
	stk_push_long(opLenStk, len);
	printf("OParenth pushed to OPSTK: %s\n", token);
	printf("OParenth pushed to OPSTK Len: %i\n", len);
      } else {
	/* pop operators from stack and write to output buffer
	 * FAIL if there is no open parenthesis in the stack.
	 */
	printf("**Close Parenth Pop\n");
	if(!write_operators_from_stack(c, opStk, opLenStk, true, true)) {
	  return false;
	}
      }

      /* check for invalid types: */
      if(prevValType != COMPILER_NO_PREV
	 && prevValType != LEXERTYPE_PARENTHESIS
	 && prevValType != LEXERTYPE_OPERATOR
	 && prevValType != LEXERTYPE_NUMBER
	 && prevValType != LEXERTYPE_STRING
	 && prevValType != LEXERTYPE_KEYVAR) {
	c->err = COMPILERERR_UNEXPECTED_TOKEN; 
	return false;
      }
      break;
    }

    case LEXERTYPE_OPERATOR: {
      /* Reads an operator from the lexer and decides whether or not to
       * place it in the opStk, in accordance with order of operations,
       * A.K.A. "precedence."
       */

      /* if the current operator precedence is >= to the one at the top of the
       * stack, push it to the stack. Otherwise, pop the stack empty. By doing
       * this, we modify the order of evaluation of operators based on their
       * precedence, a.k.a. order of operations.
       */

      /* TODO: add error checking for extra operators
       * and unmatched parenthesis */
      assert(token != NULL);
      if(operator_precedence(token, len)
	 >= topstack_precedence(opStk, opLenStk)) {
	
	/* push operator to operator stack */
	typestk_push(opStk, &token, sizeof(char*), type);
	stk_push_long(opLenStk, len);
	printf("Operator pushed to OPSTK: %s\n", token);
	printf("Operator pushed to OPSTK Len: %i\n", len);
      } else {

	/* pop operators from stack and write to output buffer */
	printf("**Popping Operators.\n");
	if(!write_operators_from_stack(c, opStk, opLenStk, true, false)) {
	  return false;
	}

	/* push operator to operator stack */
	typestk_push(opStk, &token, sizeof(char*), type);
	stk_push_long(opLenStk, len);
      }

      /* check for invalid types: */
      if(prevValType != LEXERTYPE_STRING
	 && prevValType != LEXERTYPE_NUMBER
	 && prevValType != LEXERTYPE_KEYVAR
	 && prevValType != LEXERTYPE_PARENTHESIS) {
	/* TODO: this error check does not distinguish between ( and ). Revise to
	 * ensure that parenthesis is part of a matching pair too.
	 */
	c->err = COMPILERERR_UNEXPECTED_TOKEN; 
	return false;
      }

      break;
    }

    case LEXERTYPE_ENDSTATEMENT: {

      /* check for invalid types: */
      if(prevValType == LEXERTYPE_OPERATOR
	 || prevValType == LEXERTYPE_ENDSTATEMENT) {
	c->err = COMPILERERR_UNEXPECTED_TOKEN;
	return false;
      }
      printf("ENDSTATEMENT\n");
      /*token = lexer_next(l, &type, &len);*/

      /* reached the end of the input, empty the operator stack to the output */
      printf("**End Pop\n");
      if(write_operators_from_stack(c, opStk, opLenStk, false, true)) {
	/*token = lexer_next(l, &type, &len);*/
	return true;
      } else {
	return false;
      }
      break;
    }

    default:
      c->err = COMPILERERR_UNEXPECTED_TOKEN;
      printf("Unexpected Straight Code Token: %s\n", token);
      printf("Unexpected Straight Code Token Len: %i\n", len);
      return false;
    }

    /* store the type of this token for the next iteration: */
    prevValType = type;

  } while((token = lexer_next(l, &type, &len)) != NULL);

  /* no semicolon at the end of the line, throw a fit */
  /*if(prevValType != LEXERTYPE_ENDSTATEMENT) {*/
    c->err = COMPILERERR_EXPECTED_ENDSTATEMENT;
    return false;
    /*}*/

  /* write stack pop instruction:
   * this ensures that the last return value is popped off of the stack after
   * the statement completes.
   */
  sb_append_char(c->outBuffer, OP_POP);

  /* no errors occurred */
  return true;
}
