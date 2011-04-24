/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011 Bart Trzynadlowski 
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free 
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/
 
/*
 * ROMLoad.cpp
 * 
 * ROM loading functions.
 */
 
#include <new>
#include <string.h>
#include "Supermodel.h"
#include "Pkgs/unzip.h"


/*
 * CopyRegion(dest, destOffset, destSize, src, srcSize):
 *
 * Repeatedly mirror (copy) to destination from source until destination is
 * filled.
 *
 * Parameters:
 *		dest		Destination region.
 *		destOffset	Offset within destination to begin mirroring to.
 *		destSize	Size in bytes of destination region.
 *		src			Source region to copy from.
 *		srcSize		Size of region to copy from.
 */
void CopyRegion(UINT8 *dest, unsigned destOffset, unsigned destSize, UINT8 *src, unsigned srcSize)
{
	while (destOffset < destSize)
	{
		// If we'll overrun the destination, trim the copy length
		if ((destOffset+srcSize) >= destSize)
			srcSize = destSize-destOffset;
		
		// Copy!
		memcpy(&dest[destOffset], src, srcSize);
		
		destOffset += srcSize;
	}
}

// Search for a ROM within a single game based on its CRC
static BOOL FindROMByCRCInGame(const struct GameInfo **gamePtr, int *romIdxPtr, const struct GameInfo *Game, UINT32 crc)
{
	unsigned	j;
	
	for (j = 0; Game->ROM[j].region != NULL; j++)
	{
		if (crc == Game->ROM[j].crc)	// found it!
		{
			*gamePtr = Game;
			*romIdxPtr = j;
			return OKAY;
		}
	}
	
	return FAIL;
}
	
// Search for a ROM in the complete game list based on CRC32 and return its GameInfo and ROMInfo entries
static BOOL FindROMByCRC(const struct GameInfo **gamePtr, int *romIdxPtr, const struct GameInfo *GameList, const struct GameInfo *TryGame, UINT32 crc)
{
	unsigned	i;
	
	if (TryGame != NULL)
	{
		if (FindROMByCRCInGame(gamePtr, romIdxPtr, TryGame, crc) == OKAY)
			return OKAY;
	}
	
	for (i = 0; GameList[i].title != NULL; i++)
	{
		if (FindROMByCRCInGame(gamePtr, romIdxPtr, &(GameList[i]), crc) == OKAY)
			return OKAY;
	}
	
	return FAIL;
}

static void ByteSwap(UINT8 *buf, unsigned size)
{
	unsigned	i;
	UINT8		x;
	
	for (i = 0; i < size; i += 2)
	{
		x = buf[i+0];
		buf[i+0] = buf[i+1];
		buf[i+1] = x;
	}	 
}

// Load a single ROM file
static BOOL LoadROM(UINT8 *buf, unsigned bufSize, const struct ROMMap *Map, const struct ROMInfo *ROM, unzFile zf, const char *zipFile, BOOL loadAll)
{
	char			file[2048+1];
	int				err, bytes;
	unz_file_info	fileInfo;
	unsigned		i, j, destIdx, srcIdx;
	
	// Read the file into the buffer
	err = unzGetCurrentFileInfo(zf, &fileInfo, file, 2048, NULL, 0, NULL, 0);
	if (err != UNZ_OK)
		return ErrorLog("Unable to extract a file name from %s.", zipFile);
	if (fileInfo.uncompressed_size != ROM->fileSize)
		return ErrorLog("%s in %s is not the correct size (must be %d bytes).", file, zipFile, ROM->fileSize);
	err = unzOpenCurrentFile(zf);
	if (UNZ_OK != err)
		return ErrorLog("Unable to read %s from %s.", file, zipFile);
	bytes = unzReadCurrentFile(zf, buf, bufSize);
	if (bytes != ROM->fileSize)
	{
		unzCloseCurrentFile(zf);
		return ErrorLog("Unable to read %s from %s.", file, zipFile);
	}
	err = unzCloseCurrentFile(zf);
	if (UNZ_CRCERROR == err)
		ErrorLog("CRC error reading %s from %s. File may be corrupt.", file, zipFile);
		
	// Byte swap
	if (ROM->byteSwap)
		ByteSwap(buf, ROM->fileSize);
	
	// Find out how to map the ROM and do it
	for (i = 0; Map[i].region != NULL; i++)
	{
		if (!strcmp(Map[i].region, ROM->region))
		{
			destIdx = ROM->offset;
			for (srcIdx = 0; srcIdx < ROM->fileSize; )
			{
				for (j = 0; j < ROM->groupSize; j++)
					Map[i].ptr[destIdx+j] = buf[srcIdx++];
				
				destIdx += ROM->stride;
			}
			
			return OKAY;
		}
	}
	
	if (loadAll)	// need to load all ROMs, so there should be no unmapped regions
		return ErrorLog("%s:%d: No mapping for \"%s\".", __FILE__, __LINE__, ROM->region);
	else
		return OKAY;
}	
	
/*
 * LoadROMSetFromZIPFile(Map, GameList, zipFile):
 *
 * Loads a complete ROM set from a ZIP archive. Automatically detects the game.
 * If multiple games exist within the archive, an error will be printed and all
 * but the first detected game will be ignored.
 *
 * Parameters:
 *		Map			A list of pointers to the memory buffers for each ROM 
 *					region.
 *		GameList	List of all supported games and their ROMs.
 *		zipFile		ZIP file to load from.
 *		loadAll		If true, will check to ensure all ROMs were loaded.
 *					Otherwise, omits this check and loads only specified 
 *					regions.
 *
 * Returns:
 *		Pointer to GameInfo struct for loaded game if successful, NULL 
 *		otherwise. Prints errors.
 */
const struct GameInfo * LoadROMSetFromZIPFile(const struct ROMMap *Map, const struct GameInfo *GameList, const char *zipFile, BOOL loadAll)
{
	unzFile					zf;
	unz_file_info			fileInfo;
	const struct GameInfo	*Game = NULL, *CurGame;
	int						romIdx;	// index within Game->ROM
	unsigned				romsFound[sizeof(Game->ROM)/sizeof(struct ROMInfo)], numROMs;
	int						err;
	unsigned				i, n, maxSize;
	BOOL					multipleGameError = FALSE;
	UINT8					*buf;
	
	// Try to open file
	zf = unzOpen(zipFile);
	if (NULL == zf)
	{
		ErrorLog("Unable to open %s.", zipFile);
		return NULL;
	}
		
	// Check ROMs: scan ZIP file for first known ROM and check to ensure all ROMs are present
	memset(romsFound, 0, sizeof(romsFound));
	err = unzGoToFirstFile(zf);
	if (UNZ_OK != err)
	{
		ErrorLog("Unable to read the contents of %s (code %X)", zipFile, err);
		return NULL;
	}
	for (; err != UNZ_END_OF_LIST_OF_FILE; err = unzGoToNextFile(zf))
	{
		// Identify the file we're looking at
		err = unzGetCurrentFileInfo(zf, &fileInfo, NULL, 0, NULL, 0, NULL, 0);
		if (err != UNZ_OK)
			continue;			
		if (OKAY != FindROMByCRC(&CurGame, &romIdx, GameList, Game, fileInfo.crc))
			continue;
		if (Game == NULL)	// this is the first game we've identified within the ZIP
		{
			Game = CurGame;
			DebugLog("%ROM set identified: %s (%s), %s\n", Game->id, Game->title, zipFile);
		}
		else
		{
			if (CurGame != Game)
			{
				DebugLog("%s also contains: %s (%s)\n", zipFile, CurGame->id, CurGame->title);
				if (multipleGameError == FALSE)	// only warn about this once
				{
					ErrorLog("Multiple games were found in %s; loading \"%s\".", zipFile, Game->title);
					multipleGameError = TRUE;
				}
			}
		}
		
		// If we have found a ROM for the correct game, mark it
		if (Game == CurGame)
			romsFound[romIdx] = 1;
	}
	
	if (Game == NULL)
	{
		ErrorLog("%s contains no supported games.", zipFile);
		return NULL;
	}
		
	// Compute how many ROM files this game has
	for (numROMs = 0; Game->ROM[numROMs].region != NULL; numROMs++)
		;

	// If not all ROMs were present, tell the user
	err = OKAY;
	for (i = 0; i < numROMs; i++)
	{
		if (romsFound[i] == 0)
			err |= ErrorLog("%s (CRC=%08X) is missing from %s.", Game->ROM[i].file, Game->ROM[i].crc, zipFile);
	}
	if (err != OKAY)
	{
		unzClose(zf);
		return NULL;
		//return FAIL;
	}
		
	// Allocate memory for the largest ROM to load
	maxSize = 0;
	for (i = 0; i < numROMs; i++)
	{
		if (Game->ROM[i].fileSize > maxSize)
			maxSize = Game->ROM[i].fileSize;
	}
	buf = new(std::nothrow) UINT8[maxSize];
	if (NULL == buf)
	{
		unzClose(zf);
		ErrorLog("Insufficient memory to load ROM files (%d bytes).", maxSize);
		return NULL;
	}
		
	// Load ROMs
	memset(romsFound, 0, sizeof(romsFound));
	err = unzGoToFirstFile(zf);
	if (UNZ_OK != err)
	{
		ErrorLog("Unable to read the contents of %s (code %X).", zipFile, err);
		err = FAIL;
		goto Quit;
	}
	for (; err != UNZ_END_OF_LIST_OF_FILE; err = unzGoToNextFile(zf))
	{
		err = unzGetCurrentFileInfo(zf, &fileInfo, NULL, 0, NULL, 0, NULL, 0);
		if (err != UNZ_OK)
			continue;			
		if (OKAY != FindROMByCRC(&CurGame, &romIdx, GameList, Game, fileInfo.crc))
			continue;
		if (CurGame == Game)	// if ROM belongs to correct game
		{
			if (OKAY == LoadROM(buf, maxSize, Map, &Game->ROM[romIdx], zf, zipFile, loadAll))
				romsFound[romIdx] = 1;	// success! mark as loaded
		}
	}
	
	// Ensure all ROMs were loaded
	if (loadAll)
	{
		n = 0;
		for (i = 0; i < numROMs; i++)
		{
			if (romsFound[i])
				++n;
			else
				ErrorLog("Failed to load %s (CRC=%08X) from %s.", Game->ROM[i].file, Game->ROM[i].crc, zipFile);
		}
		if (n < numROMs)
			err = FAIL;
		else
			err = OKAY;
	}
	else
		err = OKAY;
				
Quit:
	unzClose(zf);
	delete [] buf;
	return (err == OKAY) ? Game : NULL;
}
