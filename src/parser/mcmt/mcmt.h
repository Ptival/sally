/*
 * mcmt_parser_traits.h
 *
 *  Created on: Dec 1, 2014
 *      Author: dejan
 */

#pragma once

#include "parser/mcmt/mcmtLexer.h"
#include "parser/mcmt/mcmtParser.h"

namespace sal2 {
namespace parser {

template<>
struct parser_traits<INPUT_MCMT> {
  typedef pmcmtLexer pLangLexer;
  typedef pmcmtParser pLangParser;

  static
  pmcmtLexer newLexer(pANTLR3_INPUT_STREAM instream) {
    return mcmtLexerNew(instream);
  }

  static
  pmcmtParser newParser(pANTLR3_COMMON_TOKEN_STREAM instream) {
    return mcmtParserNew(instream);
  }
};

}
}
