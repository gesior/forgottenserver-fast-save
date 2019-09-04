/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2019  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef FS_PLAYERCACHE_H_3583C7C054584881856D55765DEDAFA9
#define FS_PLAYERCACHE_H_3583C7C054584881856D55765DEDAFA9

#include "player.h"
#include "thread_holder_base.h"
#include <map>

class PlayerCacheData;

using ItemBlockList = std::list<std::pair<int32_t, Item*>>;

class PlayerCacheManager : public ThreadHolder<PlayerCacheManager>
{
	public:
		PlayerCacheManager() = default;

		bool loadCachedPlayer(uint32_t guid, Player* player);
		void cachePlayer(uint32_t guid, Player* player);

		void start();
		void flush();
		void shutdown();

		void addToSaveList(uint32_t guid);

		void threadMain();

	private:
		PlayerCacheData* getCachedPlayer(uint32_t guid, bool autoCreate = false);

		bool saveCachedItems(uint32_t guid);
		bool saveItems(uint32_t guid, const ItemBlockList& itemList, DBInsert& query_insert, PropWriteStream& propWriteStream);

		Database db;
		std::thread thread;
		std::list<uint32_t> toSaveList;
		std::mutex listLock;
		std::condition_variable listSignal;

		std::map<uint32_t, PlayerCacheData*> playersCache;
};

class PlayerCacheData
{
	public:
		~PlayerCacheData();
		PlayerCacheData* clone();
		void copyDataFromPlayer(Player* player);
		void copyDataToPlayer(Player* player);

	private:
		void clear();

		Item* inventory[CONST_SLOT_LAST + 1] = {};
		std::map<uint32_t, DepotChest*> depotChests;
		Inbox* inbox = nullptr;
		int16_t lastDepotId = -1;
		std::mutex dataLock;

		friend class PlayerCacheManager;
};

#endif
