#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include <TFE_Game/igame.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Memory/memoryRegion.h>
#include "jtokenize.h"

/****************************************************************************
*                                CONSTANTS                                  *
****************************************************************************/

enum InternalConst
{
	TOKEN_MAX_LINE_SIZE = 1024,
	TOKEN_MAX_DATA_SIZE = 1024,
	TOKEN_MAX_ARGS = 32,
	TOKEN_MAX_EXPANDED_DATA_SIZE = TOKEN_MAX_ARGS * 256
};

struct InternalTokenEntry
{
	TokenId	 token;
	TokenCmd cmd;
	s32 arg;
	s32	partialSize;
	s32	minMatches;
	s32	numArgs;
	const char* parseString;
	u8*	argSizes;
};

struct InternalTokenTable
{
	InternalTokenTable* nextTable;
	void*				extraData;
	TokenParserId 		parserId;
	s32					numTokens;
	InternalTokenEntry	token[1];
};

struct InternalTokenFile
{
	FileStream* 		file;
	bool				isText;
	TokenParserId		parserId;
	InternalTokenTable*	table;
	InternalTokenEntry*	entry;
	bool				dataReady;
	u32                 checksum;
	s32					dataBufferSize;
    s32                 dataBufferOffset;
	u8	                dataBuffer[TOKEN_MAX_DATA_SIZE * 4];
};


///////////////////////////////////////////////////
// Internal Variables
///////////////////////////////////////////////////
static InternalTokenTable* s_firstTable = nullptr;
static InternalTokenEntry s_syntaxErrorEntry[] =
{
	{ -1, TokenCmd_Error, 0, 0, 0, 0, "Syntax error", nullptr },
	{ -1, TokenCmd_StopParsing, 0, 0, 0, 0, nullptr, nullptr }
};

///////////////////////////////////////////////////
// Forward Declarations
///////////////////////////////////////////////////
static s32 tokenize_ExtractArgSizes(u8* argSizes, const char* string);
static InternalTokenEntry *tokenize_LookupToken(TokenId target, InternalTokenTable* table);
static s32 tokenize_FindTargetIndex(TokenId target, TokenEntry* entryList, s32 entryListSize);
static char* tokenize_GetNextLine(FileStream* inFile, char* lineBuffer, s32 bufferSize);
static s32 tokenize_TokenizeLine(u8* dataBuffer, char* lineBuffer, InternalTokenEntry *entry);
static s32 tokenize_TokenizeUntilData(FileStream* inFile, InternalTokenTable* table, InternalTokenEntry** entry, u8* dataBuffer);
static s32 tokenize_ExtractDataToVarArgs(InternalTokenEntry* entry, InternalTokenFile* file, va_list ap);
static InternalTokenEntry *tokenize_CalcTarget(InternalTokenTable* table, InternalTokenEntry* entry);

///////////////////////////////////////////////////
// API Implementation
///////////////////////////////////////////////////

s32 tokenize_RegisterParser(TokenParserId parserId, TokenEntry* entryList, s32 entryListSize)
{
	// Allocate the table.
	InternalTokenTable* table = (InternalTokenTable*)game_alloc(sizeof(InternalTokenTable) + entryListSize * sizeof(InternalTokenEntry));
	if (!table) { return RESULT_FAILURE; }
	
	// Then initialize.
	table->nextTable = s_firstTable;
	s_firstTable = table;
	table->parserId = parserId;
	table->numTokens = entryListSize;
	
	// now scan through the entry list and compute the additional size
	TokenEntry* entry = entryList;
	InternalTokenEntry* internal = table->token;
	s32 extraDataSize = 0;
	for (s32 i = 0; i < entryListSize; i++, entry++, internal++)
	{
		// first copy in the easy data
		internal->token       = entry->token;
		internal->cmd 		  = entry->cmd;
		internal->arg 		  = entry->arg;
		internal->partialSize = 0;
		internal->minMatches  = entry->minMatches;
		internal->numArgs 	  = 0;
		internal->parseString = nullptr;
		internal->argSizes 	  = nullptr;
		
		// if we have a parse string for this command...
		if (entry->parseString)
		{
			// add the size of the parse string
			internal->parseString = entry->parseString;
			extraDataSize += (s32)strlen(internal->parseString) + 1;
		}
		
		// based on the command, we may need to convert the argument to an index
		switch (internal->cmd)
		{
			// goto on match/fail -- count the arguments, plus match the target
			case TokenCmd_GotoOnMatch:
			case TokenCmd_GotoOnFail:
				internal->numArgs = tokenize_ExtractArgSizes(nullptr, internal->parseString);
				extraDataSize += internal->numArgs;
			// goto -- just match the target
			case TokenCmd_Goto:
				internal->arg = tokenize_FindTargetIndex(internal->arg, entryList, entryListSize);
				break;
			default:
				break;
		}
	}
	
	// allocate the extra data buffer
	table->extraData = game_alloc(extraDataSize);
	if (!table->extraData)
	{
		game_free(table);
		return RESULT_FAILURE;
	}
	
	// go back through the token table and copy out the strings and argument sizes
	extraDataSize = 0;
	internal = table->token;
	for (s32 i = 0; i < entryListSize; i++, internal++)
	{
		// only do it if we have a parse string
		if (internal->parseString)
		{
			// copy out the string
			internal->parseString = strcpy((char *)table->extraData + extraDataSize, internal->parseString);
			extraDataSize += (s32)strlen(internal->parseString) + 1;

			// remove leading whitespace
			while (isspace(internal->parseString[0]))
			{
				internal->parseString += 1;
			}
			
			// compute the partial size
			const char* temp = strchr(internal->parseString, '%');
			if (!temp)
			{
				temp = internal->parseString + strlen(internal->parseString);
			}

			do
			{
				temp -= 1;
			} while (temp > internal->parseString && isspace(*temp));
			internal->partialSize = s32(temp - internal->parseString + 1);
			
			// copy out the argument info, if we're tracking arguments for this string
			if (internal->numArgs)
			{
				internal->argSizes = (u8 *)table->extraData + extraDataSize;
				tokenize_ExtractArgSizes(internal->argSizes, internal->parseString);
				extraDataSize += internal->numArgs;
			}
		}
	}
	return RESULT_SUCCESS;
}

s32 tokenize_UnregisterParser(TokenParserId parserId)
{
	InternalTokenTable* table = s_firstTable;
	InternalTokenTable* prevTable = nullptr;
	
	// find this parser
	for (; table; prevTable = table, table = table->nextTable)
	{
		if (table->parserId == parserId)
		{
			break;
		}
	}
	if (!table) { return RESULT_FAILURE; }
	
	// adjust the previous pointer
	if (prevTable) { prevTable->nextTable = table->nextTable; }
	else { s_firstTable = table->nextTable; }
	
	// free the data
	if (table->extraData)
	{
		game_free(table->extraData);
	}
	game_free(table);
	return RESULT_SUCCESS;
}

s32 tokenize_TokenizeFile(TokenParserId parserId, FileStream* inFile, FileStream* outFile)
{
	u8 dataBuffer[TOKEN_MAX_DATA_SIZE];
	bool error = false;
	
	// find this parser
	InternalTokenTable* table = s_firstTable;
	for (; table; table = table->nextTable)
	{
		if (table->parserId == parserId) { break; }
	}
	if (!table) { return RESULT_FAILURE; }
	
	// initialize the state machine
	InternalTokenEntry* entry = table->token;
	
	// write out the token header ID
	*((s32 *)dataBuffer + 0) = 0;
	*((s32 *)dataBuffer + 1) = table->parserId;
	outFile->writeBuffer(dataBuffer, 8);
	
	// loop until end-of-file
	while (1)
	{
		// process commands until we get data
		s32 size = tokenize_TokenizeUntilData(inFile, table, &entry, dataBuffer);
		if (!size) { break; }
		outFile->writeBuffer(dataBuffer, size);
	}
	
	// return happiness if no error
	return error ? RESULT_FAILURE : RESULT_SUCCESS;
}

TokenFileInst tokenize_BeginParsing(FileStream* inFile, TokenParserId parserId)
{
	// first allocate a token file
	InternalTokenFile* file = (InternalTokenFile*)game_alloc(sizeof(InternalTokenFile));
	if (!file) { return nullptr; }
	
	// now copy in the data and initialize the state
	file->file = inFile;
	file->table = nullptr;
	file->entry = nullptr;
	file->isText = true;
	file->dataReady = false;
    file->dataBufferOffset = 0;
	file->dataBufferSize = 0;
	file->parserId = parserId;
    file->checksum = parserId;

	// see if this is a token file
	u32 data = 0;
	if (inFile->readBuffer(&data, 4) != 4)
	{
		TFE_System::logWrite(LOG_WARNING, "JTokenizer", "Tokenize_BeginParsing: Premature EOF");
		game_free(file);
		return nullptr;
	}
	
	// if this is a token file, read the token ID
	if (data == 0)
	{
		// read the ID
		if (inFile->readBuffer(&data, 4) != 4)
		{
			TFE_System::logWrite(LOG_WARNING, "JTokenizer", "Tokenize_BeginParsing: Premature EOF");
			game_free(file);
			return nullptr;
		}
		
		// confirm the match
		if (TokenParserId(data) != parserId)
		{
			TFE_System::logWrite(LOG_WARNING, "JTokenizer", "Tokenize_BeginParsing: Token file is the wrong format!\n");
			game_free(file);
			inFile->seek(-8, Stream::ORIGIN_CURRENT);
			return nullptr;
		}
		
		// mark this as a tokenized file
		file->isText = false;
	}
	else // otherwise, seek back to the beginning
	{
		inFile->seek(-4, Stream::ORIGIN_CURRENT);
	}
	
	// now find a matching parser
	InternalTokenTable* table;
	for (table = s_firstTable; table; table = table->nextTable)
	{
		if (table->parserId == parserId) { break; }
	}

	// it's an error if we can't find the parser
	if (!table)
	{
		TFE_System::logWrite(LOG_WARNING, "JTokenizer", "Tokenize_BeginParsing: No parser registered for %08X!\n", parserId);
		game_free(file);
		return nullptr;
	}
		
	// initialize the state
	file->table = table;
	file->entry = table->token;
	return file;
}

s32 tokenize_EndParsing(TokenFileInst tokenFile, u32* checksum)
{
	InternalTokenFile* file = (InternalTokenFile*)tokenFile;
	if (checksum)
	{
		*checksum = file->checksum;
	}
	// free the file data
	game_free(tokenFile);
	return RESULT_SUCCESS;
}

TokenId	tokenize_GetToken(TokenFileInst tokenFile)
{
	InternalTokenFile* file = (InternalTokenFile*)tokenFile;
	
	// if the data is not ready....
	if (!file->dataReady)
	{
		if (file->isText)  // text file case.
		{
			// parse until the next token
			file->dataBufferOffset = 0;
			file->dataBufferSize = tokenize_TokenizeUntilData(file->file, file->table, &file->entry, file->dataBuffer);

			// error if we didn't get anything
			if (!file->dataBufferSize) { return Token_Invalid; }
		}
		else  // data file case.
		{
			s32 left = file->dataBufferSize - file->dataBufferOffset;

			// if we've used up half our buffer, read in the next chunk
			if (left < TOKEN_MAX_DATA_SIZE && (!file->dataBufferSize || file->dataBufferSize == sizeof(file->dataBuffer)))
			{
				memmove(file->dataBuffer, file->dataBuffer + file->dataBufferOffset, left);
				file->dataBufferSize -= file->dataBufferOffset;
				file->dataBufferOffset = 0;

				left = sizeof(file->dataBuffer) - file->dataBufferSize;
				file->dataBufferSize += file->file->readBuffer(file->dataBuffer + file->dataBufferSize, left);
			}
		}
		// data is ready
		file->dataReady = true;
	}
	// return the token
	return *(s32*)(file->dataBuffer + file->dataBufferOffset);
}

s32 tokenize_GetTokenData(TokenFileInst tokenFile, TokenId token, ...)
{
	InternalTokenFile* file = (InternalTokenFile*)tokenFile;
	s32 result = 0;
	
	// make sure we have the token and its data available
	TokenId thisToken = tokenize_GetToken(file);
	if (thisToken == Token_Invalid || thisToken != token)
	{
		return 0;
	}

	// pull the data into varargs
	va_list ap;
	va_start (ap, token);
	result = tokenize_ExtractDataToVarArgs(tokenize_LookupToken(token, file->table), file, ap);
	va_end (ap);
	return result;
}

void tokenize_Advance(TokenFileInst tokenFile)
{
	InternalTokenFile* file = (InternalTokenFile*)tokenFile;
	if (!file->isText && file->dataReady)
	{
		TokenId thisToken = tokenize_GetToken(file);
		tokenize_ExtractDataToVarArgs(tokenize_LookupToken(thisToken, file->table), file, nullptr);
	}
	// mark the data invalid; this will force the next token
	file->dataReady = false;
}

s32 tokenize_GetTokenDataAndAdvance(TokenFileInst tokenFile, TokenId token, ...)
{
	InternalTokenFile* file = (InternalTokenFile*)tokenFile;
	
	// make sure we have the token and its data available
	TokenId thisToken = tokenize_GetToken(file);
	if (thisToken == Token_Invalid || thisToken != token)
	{
		return 0;
	}

	// pull the data into varargs
	va_list ap;
	va_start(ap, token);
	s32 result = tokenize_ExtractDataToVarArgs(tokenize_LookupToken(token, file->table), file, ap);
	va_end(ap);
	
	// mark the data invalid; this will force the next token
	file->dataReady = false;
	return result;
}

///////////////////////////////////////////////////
// Internal Implementation
///////////////////////////////////////////////////
static s32 tokenize_ExtractArgSizes(u8* argSizes, const char* string)
{
	s32 count = 0;
	
	// find the first % in the string
	string = strchr(string, '%');
	while (string)
	{
		bool done = false, small = false, large = false;
		s32 size = 0;

		// now loop over characters until we figure out what this is
		while (!done)
		{
			// point to the next character and switch off of it
			string += 1;
			switch (string[0])
			{
				// these characters are effectively noops
				case '%':
					done = true;
					break;
				// this character causes the count not to be incremented
				case '*':
					count -= 1;
					break;
				// these characters are size-unrelated modifiers -- we ignore them
				case '0':	case '1':	case '2':	case '3':	case '4':
				case '5':	case '6':	case '7':	case '8':	case '9':
					break;
				// these characters modify the size of the argument
				case 'h':
					small = true;
					break;
				case 'l':
					large = true;
					break;
				// these characters signify a character argument (253 chars maximum)
				case 'c':
					size = 253;
					done = true;
					break;
				// these characters signify a string argument (254 chars maximum)
				case '[':
					do { string += 1; } while (string[0] && string[0] != ']');
				case 's':
					size = 254;
					done = true;
					break;
				// these characters signify a floating-point argument (4 or 8 bytes)
				case 'e':	case 'E':	case 'f':	case 'g':	case 'G':
					size = large ? 8 : 5;
					done = true;
					break;
				// these characters signify an integer argument
				case 'd':	case 'i':	case 'u':	case 'x':	case 'p':	case 'o':	case 'n':
					size = small ? 2 : 4;
					done = true;
					break;
				// otherwise, break
				default:
					done = true;
					break;
			}
		}			
		
		// if we got a size, record it
		if (size)
		{
			count += 1;
			if (argSizes)
			{
				*argSizes++ = size;
			}
		}

		// find the next % in the string
		string = strchr(string + 1, '%');
	}
	
	// return the result
	return count;
}

static InternalTokenEntry* tokenize_LookupToken(TokenId token, InternalTokenTable* table)
{
	InternalTokenEntry* entry = table->token;
	s32 count = table->numTokens;
	while (count--)
	{
		if (entry->token == token)
		{
			return entry;
		}
		entry++;
	}
	return nullptr;
}

static s32 tokenize_FindTargetIndex(TokenId target, TokenEntry* entryList, s32 entryListSize)
{
	// targets < 0 remain
	if (target < 0) { return target; }
	
	// loop over all the tokens in the list, looking for a match
	TokenEntry* entry = entryList;
	for (s32 i = 0; i < entryListSize; i++, entry++)
	{
		if (entry->token == target)
		{
			return i;
		}
	}
	// syntax error in any other case
	return TokenTarget_SyntaxError;
}

static char* tokenize_GetNextLine(FileStream* inFile, char* lineBuffer, s32 bufferSize)
{
	// loop because we might get NULL lines
	while (1)
	{
		char* start = inFile->getString(lineBuffer, bufferSize);
		if (!start) { return nullptr; }
		
		// skip past any whitespace at the beginning
		while (start[0] && isspace(start[0]))
		{
			start += 1;
		}
		
		// if the first character is a # or a ;, ignore
		if (start[0] == '#' || start[0] == ';' || start[0] == 0)
		{
			continue;
		}

		// now find the end-of-line
		char* end = start + strlen(start) - 1;
		while (end >= start && isspace(end[0]))
		{
			end -= 1;
		}
		
		// if this line is effectively blank, ignore it
		if (end < start)
		{
			continue;
		}
		
		// otherwise, mark the end and return the start
		end[1] = 0;
		return start;
	}
}

static s32 tokenize_TokenizeLine(u8* dataBuffer, char* lineBuffer, InternalTokenEntry* entry)
{
	// clear the token buffer to 0
	u8 tokenBuff[TOKEN_MAX_EXPANDED_DATA_SIZE];
	memset(tokenBuff, 0, sizeof(tokenBuff));

	// do the mega-sscanf into our generic buffer
	s32 count = sscanf(lineBuffer, entry->parseString,
					tokenBuff + ( 0 * 256), tokenBuff + ( 1 * 256), tokenBuff + ( 2 * 256), tokenBuff + ( 3 * 256),
					tokenBuff + ( 4 * 256), tokenBuff + ( 5 * 256), tokenBuff + ( 6 * 256), tokenBuff + ( 7 * 256),
					tokenBuff + ( 8 * 256), tokenBuff + ( 9 * 256), tokenBuff + (10 * 256), tokenBuff + (11 * 256),
					tokenBuff + (12 * 256), tokenBuff + (13 * 256), tokenBuff + (14 * 256), tokenBuff + (15 * 256),
					tokenBuff + (16 * 256), tokenBuff + (17 * 256), tokenBuff + (18 * 256), tokenBuff + (19 * 256),
					tokenBuff + (20 * 256), tokenBuff + (21 * 256), tokenBuff + (22 * 256), tokenBuff + (23 * 256),
					tokenBuff + (24 * 256), tokenBuff + (25 * 256), tokenBuff + (26 * 256), tokenBuff + (27 * 256),
					tokenBuff + (28 * 256), tokenBuff + (29 * 256), tokenBuff + (30 * 256), tokenBuff + (31 * 256));

	// if the count is not right, see if this is still a partial candidate
	if (count != entry->numArgs)
	{
		// eof case -- we could still generate a partial match for 0
		if (count == EOF && !strncmp(lineBuffer, entry->parseString, entry->partialSize))
		{
			count = 0;
		}
		
		// if we didn't make our partial quota, return 0
		if (count < entry->minMatches)
		{
			return 0;
		}
	}
	
	// store the token first, in INTEL format
	u8* outData = dataBuffer;
	*(s32*)outData = entry->token;
	outData += 4;
	
	// store a byte with the number of arguments we got
	*outData = count;
	outData += 1;
	
	// now go through and generate the line buffer, outputting all values in INTEL format
	u8* tokenData = tokenBuff;
	for (s32 i = 0; i < count; i++, tokenData += 256)
	{
		// switch off the data size
		switch (entry->argSizes[i])
		{
			// 2-byte case
			case 2:
				*(s16 *)outData = *(s16*)tokenData;
				outData += 2;
				break;
			// 4-byte case
			case 4:
			case 5:	// (5 is 4, but with a floating point value) 
				*(u32*)outData = *(u32*)tokenData;
				outData += 4;
				break;
			// 8-byte case
			case 8:
				*(f64*)outData = *(f64*)tokenData;
				outData += 8;
				break;
			// n-byte case
			case 253:
			case 254:
			{
				s32 len = (s32)strlen((char *)tokenData);
				strcpy((char *)outData, (char *)tokenData);
				outData += len + 1;
			} break;
			// error case
			default:
				TFE_System::logWrite(LOG_ERROR, "JTokenizer", "tokenize_TokenizeLine: An invalid argument size was detected!");
				return 0;
		}
	}
	// return the size of data generated
	return s32(outData - dataBuffer);
}

static s32 tokenize_TokenizeUntilData(FileStream* inFile, InternalTokenTable* table, InternalTokenEntry** _entry, u8* dataBuffer)
{
	static const char* barfLine = "";
	static char lineBuffer[TOKEN_MAX_LINE_SIZE];
	InternalTokenEntry* entry = *_entry;
	bool done = false;
	s32 size = 0;
	char* line;
	
	// loop until end-of-file
	while (!size && !done && (line = tokenize_GetNextLine(inFile, lineBuffer, sizeof(lineBuffer))))
	{
		// process commands until we barf
		while (!done && !size)
		{
			// switch off the command
			size = 0;
			switch (entry->cmd)
			{
				// no-op -- just go on to the next entry
				case TokenCmd_Nop:
					entry += 1;
					break;
				// goto on match token -- attempt to parse
				case TokenCmd_GotoOnMatch:
					barfLine = entry->parseString;
					size = tokenize_TokenizeLine(dataBuffer, line, entry);
					
					// if we match, go to the match token, otherwise skip ahead
					if (!size) { entry += 1; }
					else { entry = tokenize_CalcTarget(table, entry); }
					break;
				// goto on fail token -- attempt to parse
				case TokenCmd_GotoOnFail:
					barfLine = entry->parseString;
					size = tokenize_TokenizeLine (dataBuffer, line, entry);
					
					// if we fail, go to the failure token, otherwise skip ahead
					if (!size) { entry = tokenize_CalcTarget(table, entry); }
					else { entry += 1; }
					break;
				// goto token -- change entries
				case TokenCmd_Goto:
					entry = tokenize_CalcTarget (table, entry);
					break;
				// error token -- display the line and the error
				case TokenCmd_Error:
					TFE_System::logWrite(LOG_ERROR, "JTokenizer", "Tokenize_TokenizeFile: %s\n", entry->parseString);
					TFE_System::logWrite(LOG_ERROR, "JTokenizer", "     Offening line: %s\n", lineBuffer);
					TFE_System::logWrite(LOG_ERROR, "JTokenizer", "     Was expecting: %s\n", barfLine);
					entry += 1;
					break;
				// stop parsing -- signal that we are finished
				case TokenCmd_StopParsing:
					done = true;
					break;
			}
		}
	}
	// update the entry pointer and return the size of data
	*_entry = entry;
	return size;
}

static s32 tokenize_ExtractDataToVarArgs(InternalTokenEntry* entry, InternalTokenFile* file, va_list ap)
{
	u8 dummyBuffer[256];
	u8* data = file->dataBuffer + file->dataBufferOffset;
	u32 csum = file->checksum;
	void *arg;
	
	// make sure we got a real entry
	if (!entry)
	{
		TFE_System::logWrite(LOG_ERROR, "JTokenizer", "Invalid token entry!");
		return 0;
	}
	
	// start right after the token itself
    csum += *(u32*)data;
	data += 4;
	
	// read the number of arguments
	s32 args = *data;
	data += 1;

	// now go through and generate the line buffer, outputting all values in INTEL format
	for (s32 i = 0; i < args; i++)
	{
		// get the data pointer
		arg = ap ? va_arg (ap, void *) : dummyBuffer;
		
		// switch off the data size
		switch (entry->argSizes[i])
		{
			// 2-byte case
			case 2:
				*(s16 *)arg = *(s16*)data;
				csum += *(s16*)arg;
				data += 2;
				break;
			// 4-byte case
			case 4:
				*(u32*)arg = *(u32*)data;
				csum += *(u32*)arg;
				data += 4;
				break;
			// 5-byte case
			case 5:
				*(u32*)arg = *(u32*)data;
				csum += (s32)(*(f32*)arg * 100.0f + 0.5f);
				data += 4;
				break;
			// 8-byte case
			case 8:
				*(f64*)arg = *(f64*)data;
				csum += (s32)(*(f64*)arg * 100.0f);
				data += 8;
				break;
			// n-byte case
			case 253:
			{
				s32 count = (s32)strlen((char *)data) + 1;
				u8* tmp = (u8*)arg;
				while (count--)
				{
					csum += *tmp++ = *data++;
				}
			} break;
			case 254:
			{
				s32 count = (s32)strlen((char *)data) + 1;
				u8* tmp = (u8*)arg;
				while (count--)
				{
					csum += *tmp++ = *data++;
				}
			} break;
			// error case
			default:
				TFE_System::logWrite(LOG_ERROR, "JTokenizer", "Tokenize_GetCurrentTokenData: An invalid argument size was detected!\n");
				return 0;
		}
	}

	// slide the data buffer down
	if (!file->isText)
	{
		file->dataBufferOffset = s32(data - file->dataBuffer);
	}

	// return the argument count
	file->checksum = csum;
	return args;
}

static InternalTokenEntry* tokenize_CalcTarget(InternalTokenTable* table, InternalTokenEntry* entry)
{
	if (entry->arg == TokenTarget_SyntaxError)
	{
		return s_syntaxErrorEntry;
	}
	else if (entry->arg == TokenTarget_Next)
	{
		return entry += 1;
	}
	return table->token + entry->arg;
}
