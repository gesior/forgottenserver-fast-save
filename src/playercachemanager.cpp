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

#include "otpch.h"

#include "playercachemanager.h"

#include "game.h"
#include "configmanager.h"

extern Game g_game;
extern ConfigManager g_config;

bool PlayerCacheManager::loadCachedPlayer(uint32_t guid, Player* player)
{
	PlayerCacheData* playerCacheData = getCachedPlayer(guid);

	if (!playerCacheData) {
		return false;
	}

	std::cout << "loadCachedPlayer, load cache: " << guid << std::endl;
	double s = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	playerCacheData->copyDataToPlayer(player);
	double e = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::cout << "----BENCH: copyDataToPlayer: " << (e - s) << std::endl;

	return true;
}

void PlayerCacheManager::cachePlayer(uint32_t guid, Player* player)
{
	PlayerCacheData* playerCacheData = getCachedPlayer(guid, true);

	std::cout << "cachePlayer, update cache: " << guid << std::endl;
	std::cout << "loadCachedPlayer, load cache: " << guid << std::endl;
	double s = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	playerCacheData->copyDataFromPlayer(player);
	double e = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::cout << "----BENCH: copyDataFromPlayer: " << (e - s) << std::endl;
	addToSaveList(guid);
}

PlayerCacheData* PlayerCacheManager::getCachedPlayer(uint32_t guid, bool autoCreate /* = false */)
{
	PlayerCacheData* playerCacheData;

	std::cout << "getCachedPlayer, guid: " << guid << std::endl;
	listLock.lock();
	auto it = playersCache.find(guid);
	if (it == playersCache.end()) {
		if (autoCreate) {
			std::cout << "getCachedPlayer, not found cache: " << guid << std::endl;
			playerCacheData = new PlayerCacheData();
			std::cout << "getCachedPlayer, create cache: " << guid << std::endl;
			playersCache.emplace(guid, playerCacheData);
		}
		else {
			listLock.unlock();
			return nullptr;
		}
	}
	else {
		std::cout << "getCachedPlayer, found cache: " << guid << std::endl;
		playerCacheData = it->second;
	}

	listLock.unlock();
	return playerCacheData;
}

void PlayerCacheManager::start()
{
	db.connect();
	ThreadHolder::start();
}

void PlayerCacheManager::threadMain()
{
	std::unique_lock<std::mutex> listLockUnique(listLock, std::defer_lock);
	while (getState() != THREAD_STATE_TERMINATED) {
		listLockUnique.lock();
		if (toSaveList.empty()) {
			listSignal.wait(listLockUnique);
		}

		if (!toSaveList.empty()) {
			uint32_t guidToSave = std::move(toSaveList.front());
			toSaveList.pop_front();
			listLockUnique.unlock();
			if (!saveCachedItems(guidToSave)) {
				std::cout << "Error while saving player items: " << guidToSave << std::endl;
			}
		}
		else {
			listLockUnique.unlock();
		}
	}
}

void PlayerCacheManager::addToSaveList(uint32_t guid)
{
	std::cout << "addToSaveList, guid: " << guid << std::endl;
	bool signal = false;
	listLock.lock();

	if (getState() == THREAD_STATE_RUNNING) {
		signal = toSaveList.empty();
		toSaveList.emplace_back(guid);
	}
	listLock.unlock();

	if (signal) {
		listSignal.notify_one();
	}
}

bool PlayerCacheManager::saveItems(uint32_t guid, const ItemBlockList& itemList, DBInsert& query_insert, PropWriteStream& propWriteStream)
{
	std::ostringstream ss;

	using ContainerBlock = std::pair<Container*, int32_t>;
	std::list<ContainerBlock> queue;

	int32_t runningId = 100;

	Database& db = Database::getInstance();
	for (const auto& it : itemList) {
		int32_t pid = it.first;
		Item* item = it.second;
		++runningId;

		propWriteStream.clear();
		item->serializeAttr(propWriteStream);

		size_t attributesSize;
		const char* attributes = propWriteStream.getStream(attributesSize);

		ss << guid << ',' << pid << ',' << runningId << ',' << item->getID() << ',' << item->getSubType() << ',' << db.escapeBlob(attributes, attributesSize);
		if (!query_insert.addRow(ss)) {
			return false;
		}

		if (Container * container = item->getContainer()) {
			queue.emplace_back(container, runningId);
		}
	}

	while (!queue.empty()) {
		const ContainerBlock& cb = queue.front();
		Container* container = cb.first;
		int32_t parentId = cb.second;
		queue.pop_front();

		for (Item* item : container->getItemList()) {
			++runningId;

			Container* subContainer = item->getContainer();
			if (subContainer) {
				queue.emplace_back(subContainer, runningId);
			}

			propWriteStream.clear();
			item->serializeAttr(propWriteStream);

			size_t attributesSize;
			const char* attributes = propWriteStream.getStream(attributesSize);

			ss << guid << ',' << parentId << ',' << runningId << ',' << item->getID() << ',' << item->getSubType() << ',' << db.escapeBlob(attributes, attributesSize);
			if (!query_insert.addRow(ss)) {
				return false;
			}
		}
	}
	return query_insert.execute();
}

bool PlayerCacheManager::saveCachedItems(uint32_t guid)
{
	std::cout << "saveItems, guid: " << guid << std::endl;

	double s = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	PlayerCacheData* playerCacheData = getCachedPlayer(guid);
	double e = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	if (!playerCacheData) {
		return false;
	}
	std::cout << "----BENCH: get cache: " << (e-s) << std::endl;

	s = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	PlayerCacheData* playerCacheDataClone = playerCacheData->clone();
	e = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::cout << "----BENCH: clone cache: " << (e - s) << std::endl;

	s = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::ostringstream query;
	PropWriteStream propWriteStream;
	//item saving
	query << "DELETE FROM `player_items` WHERE `player_id` = " << guid;
	if (!db.executeQuery(query.str())) {
		return false;
	}

	DBInsert itemsQuery("INSERT INTO `player_items` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ", &db);

	ItemBlockList itemList;
	for (int32_t slotId = 1; slotId <= 10; ++slotId) {
		Item* item = playerCacheDataClone->inventory[slotId];
		if (item) {
			itemList.emplace_back(slotId, item);
		}
	}

	if (!saveItems(guid, itemList, itemsQuery, propWriteStream)) {
		return false;
	}

	if (playerCacheDataClone->lastDepotId != -1) {
		//save depot items
		query.str(std::string());
		query << "DELETE FROM `player_depotitems` WHERE `player_id` = " << guid;

		if (!db.executeQuery(query.str())) {
			return false;
		}

		DBInsert depotQuery("INSERT INTO `player_depotitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ", &db);
		itemList.clear();

		for (const auto& it : playerCacheDataClone->depotChests) {
			DepotChest* depotChest = it.second;
			for (Item* item : depotChest->getItemList()) {
				itemList.emplace_back(it.first, item);
			}
		}

		if (!saveItems(guid, itemList, depotQuery, propWriteStream)) {
			return false;
		}
	}

	//save inbox items
	query.str(std::string());
	query << "DELETE FROM `player_inboxitems` WHERE `player_id` = " << guid;
	if (!db.executeQuery(query.str())) {
		return false;
	}

	DBInsert inboxQuery("INSERT INTO `player_inboxitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ", &db);
	itemList.clear();

	for (Item* item : playerCacheDataClone->inbox->getItemList()) {
		itemList.emplace_back(0, item);
	}

	if (!saveItems(guid, itemList, inboxQuery, propWriteStream)) {
		return false;
	}

	e = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::cout << "----BENCH: write to db: " << (e - s) << std::endl;

	s = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	delete playerCacheDataClone;
	e = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::cout << "----BENCH: delete cache clone: " << (e - s) << std::endl;
	return true;
}

void PlayerCacheManager::flush()
{
	std::unique_lock<std::mutex> guard{ listLock };
	while (!toSaveList.empty()) {
		auto guidToSave = std::move(toSaveList.front());
		toSaveList.pop_front();
		guard.unlock();
		if (!saveCachedItems(guidToSave)) {
			std::cout << "Error while saving player items: " << guidToSave << std::endl;
		}
		guard.lock();
	}
}

void PlayerCacheManager::shutdown()
{
	listLock.lock();
	setState(THREAD_STATE_TERMINATED);
	listLock.unlock();
	flush();
	listSignal.notify_one();
}

PlayerCacheData::~PlayerCacheData() {
	clear();
}

PlayerCacheData* PlayerCacheData::clone() {
	dataLock.lock();

	PlayerCacheData* clone = new PlayerCacheData();

	for (uint8_t slotId = CONST_SLOT_FIRST; slotId <= CONST_SLOT_LAST; ++slotId) {
		Item* slotItem = inventory[slotId];

		if (slotItem) {
			clone->inventory[slotId] = slotItem->cloneWithoutDecay();
		}
	}

	for (const auto& it : depotChests) {
		auto depotId = it.first;
		DepotChest* depotChest = it.second;

		clone->depotChests[depotId] = (DepotChest*)depotChest->cloneWithoutDecay();
	}

	if (inbox) {
		clone->inbox = (Inbox*)inbox->cloneWithoutDecay();
	}

	clone->lastDepotId = lastDepotId;

	dataLock.unlock();

	return clone;
}

void PlayerCacheData::copyDataFromPlayer(Player* player) {
	clear();

	dataLock.lock();

	for (uint8_t slotId = CONST_SLOT_FIRST; slotId <= CONST_SLOT_LAST; ++slotId) {
		Item* slotItem = player->inventory[slotId];

		if (slotItem) {
			inventory[slotId] = slotItem->cloneWithoutDecay();
			inventory[slotId]->setParent(nullptr);
		}
	}

	for (const auto& it : player->depotChests) {
		auto depotId = it.first;
		DepotChest* depotChest = it.second;

		depotChests[depotId] = (DepotChest*)depotChest->cloneWithoutDecay();
		depotChests[depotId]->setParent(nullptr);
	}

	Inbox* playerInbox = player->inbox;

	if (playerInbox) {
		inbox = (Inbox*)playerInbox->cloneWithoutDecay();
		inbox->setParent(nullptr);
	}

	lastDepotId = player->lastDepotId;

	dataLock.unlock();
}

void PlayerCacheData::copyDataToPlayer(Player* player) {
	dataLock.lock();

	for (uint8_t slotId = CONST_SLOT_FIRST; slotId <= CONST_SLOT_LAST; ++slotId) {
		Item* slotItem = inventory[slotId];
		if (slotItem) {
			player->internalAddThing(slotId, slotItem->cloneWithoutDecay());
		}
	}

	for (const auto& it : depotChests) {
		auto depotId = it.first;
		DepotChest* depotChest = it.second;
		player->depotChests[depotId] = (DepotChest*)depotChest->cloneWithoutDecay();
	}

	if (inbox) {
		player->inbox = (Inbox*)inbox->cloneWithoutDecay();
	}

	dataLock.unlock();
}

void PlayerCacheData::clear() {
	dataLock.lock();

	for (uint8_t slotId = CONST_SLOT_FIRST; slotId <= CONST_SLOT_LAST; ++slotId) {
		if (inventory[slotId]) {
			delete inventory[slotId];
			inventory[slotId] = nullptr;
		}
	}

	for (auto& it : depotChests) {
		delete it.second;
	}
	depotChests.clear();

	if (inbox) {
		delete inbox;
		inbox = nullptr;
	}

	dataLock.unlock();
}
