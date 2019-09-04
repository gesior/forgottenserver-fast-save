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
	playerCacheData->copyDataToPlayer(player);

	return true;
}

void PlayerCacheManager::cachePlayer(uint32_t guid, Player* player)
{
	PlayerCacheData* playerCacheData = getCachedPlayer(guid, true);

	std::cout << "cachePlayer, update cache: " << guid << std::endl;
	playerCacheData->copyDataFromPlayer(player);
	addTask(guid);
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
			taskSignal.wait(listLockUnique);
		}

		if (!toSaveList.empty()) {
			
			uint32_t guidToSave = std::move(toSaveList.front());
			toSaveList.pop_front();
			listLockUnique.unlock();
			runTask(guidToSave);
			
		}
		else {
			listLockUnique.unlock();
		}
	}
}

void PlayerCacheManager::addTask(uint32_t guid)
{
	bool signal = false;
	listLock.lock();
	
	if (getState() == THREAD_STATE_RUNNING) {
		signal = toSaveList.empty();
		toSaveList.emplace_back(guid);
	}
	listLock.unlock();
	
	if (signal) {
		taskSignal.notify_one();
	}
}

void PlayerCacheManager::runTask(uint32_t guid)
{
	/*
	if (task.store) {
		result = db.storeQuery(task.query);
		success = true;
	}
	else {
		result = nullptr;
		success = db.executeQuery(task.query);
	}

	if (task.callback) {
		g_dispatcher.addTask(createTask(std::bind(task.callback, result, success)));
	}
	*/
}

void PlayerCacheManager::flush()
{
	std::unique_lock<std::mutex> guard{ listLock };
	while (!toSaveList.empty()) {
		auto task = std::move(toSaveList.front());
		toSaveList.pop_front();
		guard.unlock();
		runTask(task);
		guard.lock();
	}
}
void PlayerCacheManager::shutdown()
{
	listLock.lock();
	setState(THREAD_STATE_TERMINATED);
	listLock.unlock();
	flush();
	taskSignal.notify_one();
}

PlayerCacheData::~PlayerCacheData() {
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

PlayerCacheData* PlayerCacheData::clone() {
	dataLock.lock();

	PlayerCacheData *clone = new PlayerCacheData();

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

	dataLock.unlock();

	return clone;
}

void PlayerCacheData::copyDataFromPlayer(Player* player) {
	dataLock.lock();

	for (uint8_t slotId = CONST_SLOT_FIRST; slotId <= CONST_SLOT_LAST; ++slotId) {
		if (inventory[slotId]) {
			delete inventory[slotId];
			inventory[slotId] = nullptr;
		}
	}

	for (uint8_t slotId = CONST_SLOT_FIRST; slotId <= CONST_SLOT_LAST; ++slotId) {
		Item* slotItem = player->inventory[slotId];

		if (slotItem) {
			inventory[slotId] = slotItem->cloneWithoutDecay();
			inventory[slotId]->setParent(nullptr);
		}
	}

	for (auto& it : depotChests) {
		delete it.second;
	}
	depotChests.clear();

	for (const auto& it : player->depotChests) {
		auto depotId = it.first;
		DepotChest* depotChest = it.second;

		depotChests[depotId] = (DepotChest*)depotChest->cloneWithoutDecay();
		depotChests[depotId]->setParent(nullptr);
	}

	if (inbox) {
		delete inbox;
		inbox = nullptr;
	}

	Inbox* playerInbox = player->getInbox();

	if (playerInbox) {
		inbox = (Inbox*)playerInbox->cloneWithoutDecay();
		inbox->setParent(nullptr);
	}

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
