------------------------------------------------------------------------------
-- Havok Script bugs

Bug{
brief = [[Indeterminate value can decide what BOM is detected]],
culprit = {
	[[hks::CompilerReader::getNext]],
	[[hks::CompilerLexer::readBOM]]
},
example = '\xFE\xFF', -- utf16 BOM
description = [[
hks::CompilerReader::getNext() increments the buffer position pointer every
time it is called, even when the stream's state is STREAM_END and there are no
more input bytes left. In the case of a source file that contains only a
big-endian UTF16 byte order mark, the buffer position pointer will eventually
point to potentially inaccessible/uninitialized memory, as it gets incremented
past the end boundary of the buffer returned by the user lua_Reader function
provided when calling hks_load. This is because hks::CompilerLexer::readBOM(),
upon advancing over the UTF16 BOM, will peek the next byte regardless of the
state of the input stream. It will then be reading from memory that it should
not be reading from, and if the next 2 bytes it reads happen to be zero, a
UTF32 BOM will be detected instead of UTF16.
]],
fix = [[
Only peek each of the next 2 bytes after the UTF16 BOM if
hks::CompilerReader::hasMore() returns true.
]],
comment = [[
I discovered this bug accidentally when testing source files on both Black Ops
II and Black Ops III. When I tested the file 'utf16be_bom.lua', which is a Lua
source file containing only a big-endian UTF16 byte-order-mark, the 2 games
generated differing error messages. Black Ops III detected a big-endian UTF16
BOM, and Black Ops II detected a little-endian UTF32 BOM. After several more
invokations of both game's compilers with the same source file, each game's
respective output remained the same. I then consulted the
disassembly/decompilation of each game in Ghidra and could not find any
differences in either game's logic that would cause differing results from
processing `utf16be_bom.lua'. After messing around with my own code and
running the test some more, I realized the problem was in fact ill-formed code
in Havok Script that was being enabled by my interface code to generate the
bug.
]]
}

Bug{
brief = [[Incorrect BOM detection for little-endian UTF32 input files]],
culprit = [[hks::CompilerLexer::readBOM]],
example = '\xFF\xFE\x00\x00', -- utf32 BOM
description = [[
Little-endian UTF32 byte order marks are detected as little-endian UTF16.
]],
fix = [[
Fix incorrect classification of byte order marks in readBOM().
]]
}

Bug{
brief = [[hksi_hksL_loadbuffer can dereference a NULL pointer]],
culprit = [[hksi_hksL_loadbuffer]],
description = [[
The API hksi_hksL_loadbuffer has a parameter 'name' of type 'const char *'.
This parameter is allowed to be NULL, as the API will assign it a default
value if it is. Before checking if it is NULL, it compares its value to
another parameter 'buff', and if they compare unequal, it calculates the
length of the string 'name', which triggers a segmentation fault if it is
NULL.
]],
fix = [[
Check if 'name' is not NULL when comparing it to 'buff'.
]]
}

Bug{
brief = [[Quotes are unbalanced around number tokens in parser errors]],
culprit = {
	[[hks::CompilerLexer::readNumeral]],
	[[hks::SimpleCompilerState::signalError]]
},
example = [[
local 1 = "hello";
]],
description = [[
Error messages of the form "<token> expected near '<number>'", the close quote
around <number> is not printed. These errors are generated in
hks::SimpleCompilerState::signalError(). To build the error string, 4 strings
are pushed to the stack and concatenated with hksi_lua_concat. The first
string pushed is of the form <file>:<line>: <error>, where <error> is the
'<token> expected' part of the error message. The second and fourth strings
pushed are both literals, " near '" and "'" respectively. The third string is
what causes the bug. It is obtained by calling
hks::SimpleCompilerState::pushTokenText(), which returns the string form of a
given token. If the given token is TK_NUMBER, TK_NAME, or TK_STRING, the token
buffer is used directly as input to hksi_lua_pushlstring, providing as
arguments the buffer pointer and the data length values of the token buffer,
with calls to hks::CompilerLexer::getCurrentTokBuffer()->getBuffer() and
hks::CompilerLexer::getCurrentTokBuffer()->getLength(), respectively. The data
length value is incremented each time a new character is saved to the token
buffer, which is done by calling hks::HksCharacterBuffer::push(). When the
token type is TK_NUMBER, the token buffer is written to by
hks::CompilerLexer::readNumeral(), and after writing the complete number
string to the buffer, it pushes a null-byte to terminate the string, which
increments the data length. Thus, the third string pushed for hksi_lua_concat
contains an embedded null character, and when all 4 strings are concatenated,
the resulting string, when processed as a C-string, will appear to end before
the fourth string begins, leaving out the final close quote.
So an error message that is supposed to say
"<name> expected near '1'" will actuall say
"<name> expected naer '1", and the actual result of concatenation would be
"<name> expected near '1\0'"
]],
fix = [[
Use one less then the length of the third string when pushing it for
hksi_lua_concat to exclude the null terminator.
]]
}

local function genlongname()
	local name = string.rep("t123456789", 18)
	local t = { name, name, name }
	return (table.concat( t, "." ))
end


Bug{
brief = [[Too-long function names with multiple parts cause buffer overrun]],
culprit = [[hks::CodeGenerator::buildFunctionName]],
example = genlongname(),
description = [[
When a function name has multiple parts, e.g. <name>.<field1>.<field2>...,
and the length limit of 512 has been reached, if there is still another name
part left to add, a '.' or ':' will be added to the buffer before checking if
there is space left to write the next name part. A buffer overrun will occur
if the function name is longer than 512 and has at least one '.' or ':'.
]],
fix = [[Check if there is space in the buffer before adding '.' or ':'.]]
}

Bug{
brief =
[[Function names greater than 511 will have an embedded null character in
debug info]],
culprit = [[hks::CodeGenerator::buildFunctionName]],
example = string.rep("t123456789", 52),
description = [[
If the built function name is 512 characters or longer, a null terminator will
be written to position 511 of the buffer. Otherwise, a null terminator will
be written to the current position in the buffer. However, when calling
hks::StringTable::internPinned(), the length passed to it is allowed to be
exactly 512, but no more, meaning if the function name is 512 or longer, only
the first 511 characters will be used, but the length will be 512, thus
including the null terminator in the string.
]],
fix = [[Pass a length of no more than 511 to internPinned().]]
}

Bug{
brief = [[Extra values on the right side of a typed local statement cause a
dereference of a NULL pointer]],
example = [[
-- crashes Sekiro: a typed local statement with extra RHS expressions
local i:ifunction = function() end, ""
]]
description = [[
If there are extra values on the right-hand-side of a typed local definition,
the compiler will crash trying to access a NULL pointer. This happens because
the dynamic vector containing the pending type constraints is not checked to
make sure it has an element remaining. After all the type constraints are
applied and removed, the remaining extra right-hand-side values will cause
another access to the now empty type constraints array, which is now NULL,
resulting in a segmentation fault after dereferencing a NULL pointer.
]],
fix = [[Check if there are type constraints remaining before accessing the
array.]]
}
