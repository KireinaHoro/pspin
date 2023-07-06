%{
#include "parser.hpp"
#define YY_NO_UNPUT

%}

digit         [0-9]
letter        [a-zA-Z]

%%
"byte"               { return BYTE_; }
"char"               { return CHAR_; }
"int"                { return INT_; }
"double"             { return DOUBLE_; }
"float"              { return FLOAT_; }
"int32"              { return INT32_T_; }	
"int64"              { return INT64_T_; }
"uchar"              { return UNSIGNED_CHAR_; }
"short"              { return SHORT_; }
"ushort"             { return UNSIGNED_SHORT_; }
"long"               { return LONG_; }
"ulong"              { return UNSIGNED_LONG_; }
"longdouble"         { return LONG_DOUBLE_; }
"ulonglong"          { return UNSIGNED_LONG_LONG_; }
"unsigned"           { return UNSIGNED_; }
"null"               { return DATATYPE_NULL_; }


"ctg"                { return CONTIGUOUS; }
"vec"                { return VECTOR; }
"hvec"               { return HVECTOR; }
"hidx"               { return HINDEXED; }
"idxb"               { return INDEXEDBLOCK; }
"struct"             { return STRUCT; }
"resized"            { return RESIZED; }
"subarray"           { return SUBARRAY; }


[-]                  { return ELEM; }

{digit}+|-{digit}+   { yylval.val = atoll(yytext); return NUM; }


[\[\]\(\):,]         { return yytext[0]; }

[ \t\n\r]            /* skip whitespace */

"//".*               { /*Ignore comment*/ }

.                    { printf("Unknown character [%c]\n", yytext[0]); return UNKNOWN; }
%%

int yywrap(void){return 1;}

