/////////////////////////////////////////////////////////
// Tokenizer / Parser introduced with Outlaws
/////////////////////////////////////////////////////////
#pragma once
#include <TFE_System/types.h>
#include <TFE_FileSystem/filestream.h>

enum TokenConst
{
	Token_Invalid = -1,
	TokenTarget_SyntaxError = -1,
	TokenTarget_Next = -2
};

enum ResultCode
{
	RESULT_SUCCESS = 0,
	RESULT_FAILURE = 1,
};

typedef u32 TokenParserId;
typedef void* TokenFileInst;
typedef s32 TokenId;

// Tokenizer opcodes.
enum TokenCmd
{
	TokenCmd_Nop,
	TokenCmd_GotoOnMatch,
	TokenCmd_GotoOnFail,
	TokenCmd_Goto,
	TokenCmd_Error,
	TokenCmd_StopParsing
};

struct TokenEntry
{
	TokenId	 token;
	TokenCmd cmd;
	s32		 arg;
	s32		 minMatches;
	char*	 parseString;
};

// these are the core tokenizer opcodes
#define TOKEN_NOOP(l)						{ (l), TokenCmd_Nop, 0, 0, NULL },
#define TOKEN_PARTIAL_MATCH_GOTO(l,x,p,s)	{ (l), TokenCmd_GotoOnMatch, (x), (p), (s) },
#define TOKEN_PARTIAL_FAIL_GOTO(l,x,p,s)	{ (l), TokenCmd_GotoOnFail, (x), (p), (s) },
#define TOKEN_GOTO(l,x)						{ (l), TokenCmd_Goto, (x), 0, NULL },
#define TOKEN_ERROR(l,s)					{ (l), TokenCmd_Error, 0, 0, (s) },
#define TOKEN_END(l)						{ (l), TokenCmd_StopParsing, 0, 0, NULL },

// these are variations of the above for convenience & readability
#define TOKEN_MATCH_GOTO(l,x,s)		  TOKEN_PARTIAL_MATCH_GOTO(l, x, 1000, s)
#define TOKEN_FAIL_GOTO(l,x,s)		  TOKEN_PARTIAL_FAIL_GOTO(l, x, 1000, s)

#define TOKEN_OPTIONAL(l,s)			  TOKEN_FAIL_GOTO(l, TokenTarget_Next, s)
#define TOKEN_REQUIRED(l,s)			  TOKEN_FAIL_GOTO(l, TokenTarget_SyntaxError, s)
#define TOKEN_SEVERAL(l,s)			  TOKEN_MATCH_GOTO(l, l, s)

#define TOKEN_PARTIAL_OPTIONAL(l,p,s) TOKEN_PARTIAL_FAIL_GOTO(l, TokenTarget_Next, p, s)
#define TOKEN_PARTIAL_REQUIRED(l,p,s) TOKEN_PARTIAL_FAIL_GOTO(l, TokenTarget_SyntaxError, p, s)
#define TOKEN_PARTIAL_SEVERAL(l,p,s)  TOKEN_PARTIAL_MATCH_GOTO(l, l, p, s)

#define TOKEN_SYNTAX_ERROR(l)		  TOKEN_GOTO(l, TokenTarget_SyntaxError)

// register and unregister a tokenizer parser
s32 tokenize_RegisterParser(TokenParserId parserId, TokenEntry* entryList, s32 entryListSize);
s32 tokenize_UnregisterParser(TokenParserId parserId);

// tokenize a file until the parser says stop
s32	tokenize_TokenizeFile(TokenParserId parserId, FileStream* inFile, FileStream* outFile);

// begin or stop parsing a file, which could be either the original text or the tokenized representation
TokenFileInst tokenize_BeginParsing (FileStream* inFile, TokenParserId idParser);
s32			  tokenize_EndParsing (TokenFileInst pTokenFile, u32* pChecksum);

// operate on the current token and its data
TokenId	tokenize_GetToken (TokenFileInst pFile);
s32 	tokenize_GetTokenData (TokenFileInst pFile, TokenId token, ...);

// advance to the next token, or jump right ahead and get its data
void tokenize_Advance (TokenFileInst pFile);
s32  tokenize_GetTokenDataAndAdvance (TokenFileInst pFile, TokenId token, ...);
