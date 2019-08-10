#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleTextOut.h>
//#include <Protocol/Timestamp.h> MASZYNA WIRTUALNA U MNIE NIE WSPIERA => TRUDNOSCI Z OBSLUGA CZASU :/

CONST UINT32 SCREEN_WIDTH = 600, SCREEN_HEIGHT = 480;
CONST UINT32 SCREEN_X = 100, SCREEN_Y = 60;
CONST UINT32 TILE_SIZE = 32;
CONST float VEL = 6.0f, ENEMY_MOVE_MUL = 0.2f, COIN_MOVE_MUL = 0.1f;
CONST float MAX_JUMP = 100.0f, JMP_SPEED_MUL = 0.4f;
CONST UINT8 LIVES = 1;
BOOLEAN ok = 0;

//STRUCTS
	typedef struct {
		INT32 posX, posY;
		UINT32 width, height;
	} Camera;

	typedef struct {
		EFI_GRAPHICS_OUTPUT_BLT_PIXEL** sprites;
		UINT32 n;
	} SpriteSheet;

	typedef struct {
		float _posX, _posY;
		UINT32 posX, posY;

		float velX, velY;

		UINT8 width, height;
		UINT8 frameId;
		SpriteSheet* BMP;
		BOOLEAN visible;

		VOID* coin;

	} GameObject;

	typedef struct {
		GameObject go;
	
		BOOLEAN isJumping, isFalling;
		float actualHeight, startHeight;
		
		UINT8 lives, coins;
	} Player;

	typedef struct {
		GameObject go;
	
		UINT32 startX, endX;		
	} Enemy;

	typedef struct {
		UINT32 width, height;

		char* rawData;

		GameObject* tiles;
		GameObject* coins;
		Enemy* enemies;

		Player* player;

		BOOLEAN finished;

		UINT32 tilesCount, enemiesCount, coinsCount;
		UINT32 startX, startY;
		UINT32 endX, endY;
	} Level;

	typedef enum {
		groundCollison,
		platformCollision,
		coinCollision,
		none
	} Collision;
//STRUCTS

//GRAPHICS CORE
	VOID ClearFrame(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop)
	{

		EFI_GRAPHICS_OUTPUT_BLT_PIXEL skyBlue;
		skyBlue.Red = 119;
		skyBlue.Green = 181;
		skyBlue.Blue = 254;
		skyBlue.Reserved = 0;

		gop->Blt(
			gop,
			&skyBlue,
			EfiBltVideoFill,
			0, 0,
			SCREEN_X, SCREEN_Y,
			SCREEN_WIDTH, SCREEN_HEIGHT,
			0
		);
	}
	
	//WORKAROUND
	VOID ClearSides(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop)
	{
		EFI_GRAPHICS_OUTPUT_BLT_PIXEL black;
		black.Red = 0;
		black.Green = 0;
		black.Blue = 0;
		black.Reserved = 0;

		gop->Blt(
			gop,
			&black,
			EfiBltVideoFill,
			0, 0,
			SCREEN_X - TILE_SIZE, SCREEN_Y,
			TILE_SIZE, SCREEN_HEIGHT,
			0
		);
		gop->Blt(
			gop,
			&black,
			EfiBltVideoFill,
			0, 0,
			SCREEN_X + SCREEN_WIDTH, SCREEN_Y,
			TILE_SIZE, SCREEN_HEIGHT,
			0
		);
		gop->Blt(
			gop,
			&black,
			EfiBltVideoFill,
			0, 0,
			SCREEN_X, SCREEN_Y - TILE_SIZE,
			SCREEN_WIDTH, TILE_SIZE,
			0
		);
	}
//GRAPHICS CORE

//GAME CORE
	//GAME OBJECT
		VOID InitGameObject(GameObject* go, UINT32 posX, UINT32 posY)
		{
			go->velX = 0.0f;
			go->velY = 0.0f;
			go->posX = posX;
			go->posY = posY;
			go->_posX = (float)posX;
			go->_posY = (float)posY;

			go->width = go->height = TILE_SIZE;
			go->coin = NULL;

			go->frameId = 0;
			go->visible = 1;
		}

		VOID InitTile(GameObject* go, UINT32 posX, UINT32 posY, SpriteSheet* tilesBMP, CHAR8 type, GameObject* coin)
		{
			InitGameObject(go, posX, posY);
			go->frameId = type == 'G' ? 0 : (type == 'P' ? 1 : (type == 'C' ? 2 : 0));
			go->coin = type == 'C' ? (VOID*) coin : NULL;
			go->BMP = tilesBMP;
		}

		VOID InitPlayer(Player* p, UINT32 posX, UINT32 posY, SpriteSheet* playerBMP)
		{
			InitGameObject(&p->go, posX, posY);
			p->go.BMP = playerBMP;
			p->lives = LIVES;
			p->coins = 0;

			p->isFalling = 0;
			p->isJumping = 0;

			p->startHeight = 0.0f;
			p->actualHeight = 0.0f;

		}

		VOID InitCoin(GameObject* go, UINT32 posX, UINT32 posY, SpriteSheet* coinBMP)
		{
			InitGameObject(go, posX, posY);
			go->BMP = coinBMP;
			go->visible = 0;
			go->velY = VEL * COIN_MOVE_MUL;
		}

		VOID InitEnemy(Enemy* e, UINT32 startX, UINT32 endX, UINT32 posY, SpriteSheet* enemyBMP)
		{
			InitGameObject(&e->go, startX, posY);
			e->go.BMP = enemyBMP;
			e->go.velX = VEL * ENEMY_MOVE_MUL;
			e->startX = startX;
			e->endX = endX;

		}

		VOID GameObjectStop(GameObject* go)
		{
			go->velX = 0;
		}

		VOID GameObjectMoveLeft(GameObject* go)
		{
			go->velX = -1 * VEL;
		}

		VOID GameObjectMoveRight(GameObject* go)
		{
			go->velX = VEL;
		}

		VOID GameObjectJump(Player* p)
		{
			if(p->isFalling || p->isJumping)
			{
				return;
			}

			p->isJumping = 1;
			p->go.velY = -1 * VEL * JMP_SPEED_MUL;
		}

		VOID GameObjectFall(Player* p)
		{
			p->go.velY = VEL * JMP_SPEED_MUL;
			p->isJumping = 0;
			p->isFalling = 1;
		}

		VOID GameObjectStopFalling(Player* p, CONST GameObject* tile)
		{
			if(p->isJumping != 1)
			{
				p->go.velY = 0.0f;
			}
			p->isFalling = 0;
			p->go._posY = tile->_posY - p->go.height;
		}

		VOID GameObjectUpdate(GameObject* go)
		{
			go->_posX += go->velX;
			go->_posY += go->velY;

			go->posX = (UINT32)go->_posX;
			go->posY = (UINT32)go->_posY;

		}

		VOID EnemyUpdate(Enemy* enemy)
		{
			if(enemy->startX >= enemy->go.posX)
			{
				GameObjectMoveRight(&enemy->go);
				enemy->go.velX *= ENEMY_MOVE_MUL;
			}
			else if(enemy->endX <= enemy->go.posX)
			{
				GameObjectMoveLeft(&enemy->go);
				enemy->go.velX *= ENEMY_MOVE_MUL;
			}

			enemy->go.frameId = (enemy->go.frameId + 1) % 3;
			GameObjectUpdate(&enemy->go);
		}

		VOID CoinUpdate(GameObject* coin, UINT32 levelHeight)
		{
			if(coin->posY + coin->height >= levelHeight - TILE_SIZE/2)
			{
				coin->visible = 0;
			}
			if(coin->visible)
			{
				GameObjectUpdate(coin);
			}
		}

		VOID PlayerUpdate(Player* player, Collision c)
		{
			if(player->isJumping)
			{
				player->actualHeight = player->go._posY;
			}
			else
			{
				player->startHeight = player->go._posY;
			}

			if ((player->isJumping && (player->startHeight - player->actualHeight) >= MAX_JUMP) || (!player->isJumping && c == none))
			{
				GameObjectFall(player);
			}
			
			if (c == groundCollison && player->go.velX > 0)
			{
				player->go.frameId = (player->go.frameId + 1) % 4; 
			}
			else if (c == groundCollison && player->go.velX < 0)
			{
				player->go.frameId = (player->go.frameId + 1) % 4 + 4; 
			}
			
			if (player->go.velY != 0 && player->go.velX > 0)
			{
				player->go.frameId = 8;
			}
			else if (player->go.velY != 0 && player->go.velX < 0)
			{
				player->go.frameId = 9;
			}

			GameObjectUpdate(&player->go);
		}

		//SMIESZNY BUG W KOLIZJI - BARDZIEJ FICZER - WSPINACZKA
		Collision CheckTileCollisions(Player* player, GameObject* tiles, UINT32 tilesCount)
		{
			GameObject* coin;
			for(UINT32 i = 0; i < tilesCount; i++)
			{
				if(!tiles[i].visible)
				{
					continue;
				}
				
				if ((player->go._posY >= tiles[i]._posY && player->go._posY < (tiles[i]._posY + tiles[i].height))
					|| ((player->go._posY + player->go.height) > tiles[i]._posY && (player->go._posY + player->go.height) < (tiles[i]._posY + tiles[i].height)))
				{
					if((player->go._posX + player->go.width) > tiles[i]._posX && (player->go._posX + player->go.width) < (tiles[i]._posX + tiles[i].width) && player->go.velX > 0)
					{
						GameObjectStop(&player->go);
					}
					else if (player->go._posX < (tiles[i]._posX + tiles[i].width) && player->go._posX > tiles[i]._posX && player->go.velX < 0)
					{
						GameObjectStop(&player->go);
					}
				}

				if ((player->go._posX >= tiles[i]._posX && player->go._posX <= (tiles[i]._posX + tiles[i].width))
				|| ((player->go._posX + player->go.width) > tiles[i]._posX && (player->go._posX + player->go.width) < (tiles[i]._posX + tiles[i].width)))
				{
					if((player->go._posY + player->go.height) >= tiles[i]._posY && (player->go._posY + player->go.height) <=(tiles[i]._posY + tiles[i].height))
					{
						GameObjectStopFalling(player, &tiles[i]);
						return groundCollison;
					}
					else if(player->isJumping && player->go._posY >= tiles[i]._posY && player->go._posY <= (tiles[i]._posY + tiles[i].height))
					{
						tiles[i].visible = 0;
						if(tiles[i].coin != NULL)
						{
							coin = tiles[i].coin;
							coin->visible = 1;
							player->coins++;
						}
						GameObjectFall(player);
						return platformCollision;
					}
				}
			}
			return none;
		}

		BOOLEAN CheckEnemyCollisions(Player* player, Enemy* enemies, UINT32 enemiesCount)
		{
			for(UINT32 i = 0; i < enemiesCount; i++)
			{
				if(((player->go._posX >= enemies[i].go._posX && player->go._posX <= (enemies[i].go._posX + enemies[i].go.width))
					|| ((player->go._posX + player->go.width) >= enemies[i].go._posX && (player->go._posX + player->go.width) <= (enemies[i].go._posX + enemies[i].go.width)))
					&& ((player->go._posY >= enemies[i].go._posY && player->go._posY <= (enemies[i].go._posY + enemies[i].go.height))
					|| ((player->go._posY + player->go.height) >= enemies[i].go._posY && (player->go._posY + player->go.height) <= (enemies[i].go._posY + enemies[i].go.height))))
				{
					return 1;
				}
			}
			return 0;
		}

		VOID GameObjectRender(GameObject* go, EFI_GRAPHICS_OUTPUT_PROTOCOL* gop, const Camera* c)
		{
			if(!(go->visible && go->posX + go->width > c->posX && go->posX < c->posX + SCREEN_WIDTH))
			{
				return;
			}
			//PROBLEMY Z WYSWIETLANIEM FRAGMENTU SPRITE'A - BLT SOURCE_X, SOURCE_Y, DAJA EFEKTY INNE OD SPODZIEWANYCH
			gop->Blt(
				gop,
				go->BMP->sprites[go->frameId],
				EfiBltBufferToVideo,
				0, 0,
				SCREEN_X + go->posX - c->posX, SCREEN_Y + go->posY - c->posY,
				go->width, go->height,
				go->width * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
			);
		}

		VOID PrintStatus(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* output, CONST Player* player)
		{
			output->SetCursorPosition(output, 10, 1);
			Print(L" Zycia: %u, Monety: %u \n", player->lives, player->coins);
		}	
	//GAME OBJECT
	
	//LEVEL
		VOID InitLevel(Level* l, Player* player, EFI_FILE_PROTOCOL* fileProtocol, CHAR16* levelName, SpriteSheet* tilesBMP, SpriteSheet* enemyBMP, SpriteSheet* coinBMP)
		{
			l->player = player;
			l->player->lives = LIVES;
			l->finished = 0;

			EFI_FILE_PROTOCOL* levelFile;
			fileProtocol->Open(fileProtocol, &levelFile, levelName, EFI_FILE_MODE_READ, 0);

			UINTN bufferSize = sizeof(UINT32);
			levelFile->Read(levelFile, &bufferSize, (VOID*) &l->height);
			levelFile->Read(levelFile, &bufferSize, (VOID*) &l->width);
			levelFile->Read(levelFile, &bufferSize, (VOID*) &l->tilesCount);
			levelFile->Read(levelFile, &bufferSize, (VOID*) &l->enemiesCount);
			levelFile->Read(levelFile, &bufferSize, (VOID*) &l->coinsCount);
			
			bufferSize = l->width * l->height * sizeof(CHAR8);
			CHAR8* buffer = AllocatePool(bufferSize);
			levelFile->Read(levelFile, &bufferSize, (VOID*) buffer);
			l->tiles = AllocatePool(sizeof(GameObject) * l->tilesCount);
			l->enemies = AllocatePool(sizeof(Enemy) * l->enemiesCount);
			l->coins = AllocatePool(sizeof(GameObject) * l->coinsCount);

			UINT32 i = 0, j = 0, e = 0, c = 0;
			for(UINT32 y = 0; y < l->height; y++)
			{
				for(UINT32 x = 0; x < l->width; x++)
				{
					i = y * l->width + x;
					if(buffer[i] == 'P' || buffer[i] == 'G')
					{
						InitTile(&l->tiles[j], x * TILE_SIZE, y * TILE_SIZE, tilesBMP, buffer[i], NULL);
						j++;
					}
					else if(buffer[i] == 'C')
					{
						InitCoin(&l->coins[c], x * TILE_SIZE, y * TILE_SIZE, coinBMP);
						InitTile(&l->tiles[j], x * TILE_SIZE, y * TILE_SIZE, tilesBMP, buffer[i], &l->coins[c]);
						c++;
						j++;
					}
					else if (buffer[i] == 'S')
					{
						l->endX = x * TILE_SIZE;
						l->endY = y * TILE_SIZE;
						player->go._posX = (float)(x * TILE_SIZE);
						player->go._posY = (float)(y * TILE_SIZE);
					}
					else if (buffer[i] == 'M')
					{
						l->endX = x * TILE_SIZE;
						l->endY = y * TILE_SIZE;
					}
					else if (buffer[i] >='A' && buffer[i] <= 'Z')
					{
						
						for(UINT32 h = x + 1; h < l->width; h++)
						{
							if(buffer[i] == buffer[y*l->width+h])
							{
								InitEnemy(&l->enemies[e], x * TILE_SIZE, h * TILE_SIZE, y * TILE_SIZE, enemyBMP);
								e++;
								buffer[y*l->width+h] = '.';
								break;
							}
						}
					}
				}
			}

			l->width *= TILE_SIZE;
			l->height *= TILE_SIZE;

			levelFile->Close(levelFile);
			FreePool(buffer);
		}

		//BRAKUJE RESETOWANIA POZIOMU PO SMIERCI -> MOZNA GRAC TYLKO Z 1 ZYCIEM :/
		VOID LevelUpdate(Level* level)
		{
			if(level->player->go.posX >= level->endX)
			{
				level->finished = 1;
			}
			if(level->player->go._posY + level->player->go.height >= level->height || CheckEnemyCollisions(level->player, level->enemies, level->enemiesCount))
			{
				level->player->lives--;
				return;
			}
			Collision c = CheckTileCollisions(level->player, level->tiles, level->tilesCount);
			PlayerUpdate(level->player, c);
			
			for(UINT32 i = 0; i < level->enemiesCount; i++)
			{
				EnemyUpdate(&level->enemies[i]);
			}
			for(UINT32 i = 0; i < level->coinsCount; i++)
			{
				CoinUpdate(&level->coins[i], level->height);
			}
		}

		VOID LevelRender(Level* level, EFI_GRAPHICS_OUTPUT_PROTOCOL* gop, const Camera* c)
		{	
			for(UINT32 i = 0; i < level->coinsCount; i++)
			{
				GameObjectRender(&level->coins[i], gop, c);
			}
			for(UINT32 i = 0; i < level->tilesCount; i++)
			{
				GameObjectRender(&level->tiles[i], gop, c);
			}
			for(UINT32 i = 0; i < level->enemiesCount; i++)
			{
				GameObjectRender(&level->enemies[i].go, gop, c);
			}
			GameObjectRender(&level->player->go, gop, c);
		}

		VOID FreeLevel(Level* l)
		{
			FreePool(l->enemies);
			FreePool(l->tiles);
		}
	//LEVEL

	//SPRITES
		#define BMP_HEADER_SIZE 54
		SpriteSheet* LoadBMP(EFI_FILE_PROTOCOL* fileProtocol, CHAR16* bmpName)
		{
			SpriteSheet* ss = AllocatePool(sizeof(SpriteSheet));
			CHAR8 bmpHeader[BMP_HEADER_SIZE];
			UINTN bufferSize = BMP_HEADER_SIZE;

			EFI_FILE_PROTOCOL* bmpFile;
			fileProtocol->Open(fileProtocol, &bmpFile, bmpName, EFI_FILE_MODE_READ, 0);

			bmpFile->Read(bmpFile, &bufferSize, (VOID*) bmpHeader);

			UINT32 width = *(UINT32*)&bmpHeader[18], height = *(UINT32*)&bmpHeader[22];
			
			bufferSize = 3 * width* height * sizeof(CHAR8);
			CHAR8* data = AllocatePool(bufferSize);
			bmpFile->Read(bmpFile, &bufferSize, (VOID*) data);
			
			ss->n = width / TILE_SIZE;		
			ss->sprites = AllocatePool(ss->n * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL*));
			UINT32 i = 0, j = 0;
			for(UINT32 q = 0; q < ss->n; q++)
			{
				ss->sprites[q] = AllocatePool(TILE_SIZE * TILE_SIZE * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
				for(UINT32 y = 0; y < TILE_SIZE; y++)
				{
					for(UINT32 x = 0; x < TILE_SIZE; x++)
					{
						i = (TILE_SIZE - 1 - y) * 3 * width + 3 * x + 3 * q * TILE_SIZE;
						j = y * TILE_SIZE + x;
						ss->sprites[q][j].Red = data[i + 2];
						ss->sprites[q][j].Green = data[i + 1];
						ss->sprites[q][j].Blue = data[i + 0];
						ss->sprites[q][j].Reserved = 0;
					}
				}
			}
			
			bmpFile->Close(bmpFile);
			FreePool(data);
			return ss;
		}

		VOID FreeBMP(SpriteSheet* ss)
		{
			for(UINT32 i = 0; i < ss->n; i++)
			{
				FreePool(ss->sprites[i]);
			}
			FreePool(ss);
		}
	//SPRITES

	//CAMERA
		VOID InitCamera(Camera* c)
		{
			c->posX = 0;
			c->posY = 0;
			c->width = SCREEN_WIDTH;
			c->height = SCREEN_HEIGHT;
		}

		VOID CenterCamera(
			Camera* camera,
			CONST Player* player,
			CONST Level* level
		)
		{
			camera->posX = (player->go.posX + player->go.width/2) - SCREEN_WIDTH / 2;

			if(camera->posX < 0)
			{
				camera->posX = 0;
			}
			if(camera->posX > level->width - camera->width)
			{
				camera->posX = level->width - camera->width;
			}
		}
	//CAMERA
//GAME CORE

//UEFI CORE
	EFI_STATUS EFIAPI UefiMain (
		IN EFI_HANDLE			ImageHandle,
		IN EFI_SYSTEM_TABLE 	*SystemTable
	)
	{
		EFI_STATUS status;
		EFI_INPUT_KEY key;

		EFI_EVENT timerEvent;
		EFI_EVENT waitList[2];
		
		UINTN eventId;

		gBS->CreateEvent(
			EVT_TIMER,
			0,
			NULL,
			NULL,
			&timerEvent
		);
		
		gBS->SetTimer(
			timerEvent,
			TimerPeriodic,
			1000 * 1000 / 60
		);

		waitList[0] = gST->ConIn->WaitForKey;
		waitList[1] = timerEvent;

		//LOKALIZOWANIE PROTOKOLOW
			EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* simpleTextOutputProtocol;
			status = gBS->LocateProtocol(
				&gEfiSimpleTextOutProtocolGuid,
				NULL,
				(VOID**) &simpleTextOutputProtocol
			);
			if(EFI_ERROR(status))
	    	{
	    	    Print(L"Brak protokolu do obslugi tekstowego wyjscia\nNacisnij dowolny przycisk\n");
	    	    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
	    	    return EFI_UNSUPPORTED;
	    	}
			simpleTextOutputProtocol->EnableCursor(simpleTextOutputProtocol, 0);
			simpleTextOutputProtocol->SetAttribute(simpleTextOutputProtocol, EFI_TEXT_ATTR(EFI_BLACK, EFI_LIGHTGRAY));

			EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* simpleFileSystemProtocl;
			status = gBS->LocateProtocol(
				&gEfiSimpleFileSystemProtocolGuid,
				NULL,
				(VOID**) &simpleFileSystemProtocl
			);
			if(EFI_ERROR(status))
	    	{
	    	    Print(L"Brak protokolu do obslugi systemu plikow\nNacisnij dowolny przycisk\n");
	    	    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
	    	    return EFI_UNSUPPORTED;
	    	}

			EFI_FILE_PROTOCOL* fileProtocol;
			status = simpleFileSystemProtocl->OpenVolume(simpleFileSystemProtocl, &fileProtocol);
			if(EFI_ERROR(status))
	    	{
	    	    Print(L"Brak protokolu do obslugi systemu plikow\nNacisnij dowolny przycisk\n");
	    	    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
	    	    return EFI_UNSUPPORTED;
	    	}

			EFI_GRAPHICS_OUTPUT_PROTOCOL* graphicsOutputProtocol;
			status = gBS->LocateProtocol(
				&gEfiGraphicsOutputProtocolGuid,
				NULL,
				(VOID **) &graphicsOutputProtocol
			);
	    	if(EFI_ERROR(status))
	    	{
	    	    Print(L"Brak protokolu do obslugi grafiki\nNacisnij dowolny przycisk\n");
	    	    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
	    	    return EFI_UNSUPPORTED;
	    	}
		//LOKALIZOWANIE PROTOKOLOW

		SpriteSheet* tilesBMP = LoadBMP(fileProtocol, L"GAMEDATA\\tiles.bmp");
		SpriteSheet* playerBMP = LoadBMP(fileProtocol, L"GAMEDATA\\player.bmp");
		SpriteSheet* enemyBMP = LoadBMP(fileProtocol, L"GAMEDATA\\enemy.bmp");
		SpriteSheet* coinBMP = LoadBMP(fileProtocol, L"GAMEDATA\\coin.bmp");
		
		Camera camera;
		InitCamera(&camera);
		Player player;
		InitPlayer(&player, 0, 0, playerBMP);
		Level level;
		InitLevel(&level, &player, fileProtocol, L"GAMEDATA\\level.bin", tilesBMP, enemyBMP, coinBMP);

		BOOLEAN quit = 0;

		//PETLA GRY - BRAK OBSLUGI CZASU :/ GRA ZALEZNA OD PREDKOSCI KOMPUTERA
		while(!quit && player.lives > 0 && !level.finished)
		{
			ClearFrame(graphicsOutputProtocol);
			CenterCamera(&camera, &player, &level);
			LevelUpdate(&level);
			LevelRender(&level, graphicsOutputProtocol, &camera);
			ClearSides(graphicsOutputProtocol);
			PrintStatus(simpleTextOutputProtocol, &player);

			//BARDZO PROSTA OBSLUGA KLAWISZY - NIE ZAWSZE CHCE WSPOLPRACOWAC :/
			gBS->WaitForEvent(2, waitList, &eventId);
			if(eventId == 0)
			{
				gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
				gST->ConIn->Reset(gST->ConIn, 0);
			}
			else
			{
				key.ScanCode = SCAN_NULL;
			}

			switch(key.ScanCode)
			{
				case SCAN_ESC: quit = 1; break;
				case SCAN_UP: GameObjectJump(&player); break;
				case SCAN_LEFT: GameObjectMoveLeft(&player.go); break;
				case SCAN_RIGHT: GameObjectMoveRight(&player.go); break;
				case SCAN_NULL: GameObjectStop(&player.go); break;
				default: break;
			}
		}
		if(level.finished)
		{
			Print(L" GRATULACJE \n");
		}

		gST->ConIn->Reset(gST->ConIn, 0);
		Print(L" KONIEC - NACISNIJ DOWOLNY KLAWISZ \n");
		gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);

		FreeLevel(&level);
		FreeBMP(enemyBMP);
		FreeBMP(playerBMP);
		FreeBMP(tilesBMP);
		FreeBMP(coinBMP);

		gST->ConIn->Reset(gST->ConIn, 0);
		gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);

		return EFI_SUCCESS;
	}
//UEFI CORE