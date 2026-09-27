/*
 * mod-transmog-collect
 *
 * An add-on for mod-transmog's appearance collection. mod-transmog unlocks an appearance when you
 * equip an item, loot a soulbound one, or finish a quest that rewards one. This module also unlocks
 * it when:
 *
 *   - any weapon or armor piece lands in your bags: looted, bought, crafted, traded, taken from the
 *     mail or the guild bank, bound or not;
 *   - you disenchant an item (for items you had before this module was installed, or when pick-up
 *     unlocking is switched off);
 *   - you log in with it in your bags or bank.
 *
 * It writes into mod-transmog's own collection (its in-memory cache and the
 * custom_unlocked_appearances table), so appearances show up at the transmogrifier straight away.
 * It needs mod-transmog built into the same worldserver, and only uses mod-transmog functions that
 * have been there since 2022, so older mod-transmog checkouts work too.
 *
 * Released under the MIT License.
 */

#include "Bag.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Transmogrification.h"
#include "WorldSession.h"

#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
    struct Config
    {
        bool enabled = true;
        bool onPickup = true;
        bool onDisenchant = true;
        bool scanOnLogin = true;
        bool altBots = true;
        bool announce = true;
    };

    Config config;

    // The playerbots fork adds WorldSession::IsBot(); stock AzerothCore doesn't have it. Looking for
    // it at compile time lets the module build on both.
    template <typename Session, typename = void>
    struct HasIsBot : std::false_type { };

    template <typename Session>
    struct HasIsBot<Session, std::void_t<decltype(std::declval<Session&>().IsBot())>> : std::true_type { };

    template <typename Session>
    bool IsBotSession(Session* session)
    {
        if constexpr (HasIsBot<Session>::value)
            return session->IsBot();
        else
            return false;
    }

    // Random bots have accounts of their own, and every item they pick up would land in
    // custom_unlocked_appearances, which mod-transmog loads in full at startup. Alt bots (your own
    // characters played as bots) share your account, so they collect for you, but only while a real
    // player of that account is grouped with them.
    bool CanCollect(Player* player)
    {
        WorldSession* session = player ? player->GetSession() : nullptr;
        if (!config.enabled || !session || !sTransmogrification->GetUseCollectionSystem())
            return false;

        if (!IsBotSession(session))
            return true;

        if (!config.altBots)
            return false;

        Group* group = player->GetGroup();
        if (!group)
            return false;

        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (member && member != player && member->GetSession() && !IsBotSession(member->GetSession())
                && member->GetSession()->GetAccountId() == session->GetAccountId())
                return true;
        }

        return false;
    }

    std::string ItemLink(WorldSession* session, ItemTemplate const* proto)
    {
        std::string name = proto->Name1;
        if (ItemLocale const* locale = sObjectMgr->GetItemLocale(proto->ItemId))
            ObjectMgr::GetLocaleString(locale->Name, session->GetSessionDbLocaleIndex(), name);

        std::ostringstream link;
        link << "|c" << std::hex << ItemQualityColors[proto->Quality] << std::dec
             << "|Hitem:" << proto->ItemId << ":0:0:0:0:0:0:0:0|h[" << name << "]|h|r";
        return link.str();
    }

    // Mirrors mod-transmog's own AddToDatabase, which only became callable from outside in 2026.
    // Returns true when the appearance is new to the account.
    bool Collect(Player* player, ItemTemplate const* proto, bool announce)
    {
        if (!proto || (proto->Class != ITEM_CLASS_ARMOR && proto->Class != ITEM_CLASS_WEAPON))
            return false;

        if (!sTransmogrification->GetTrackUnusableItems() && !sTransmogrification->SuitableForTransmogrification(player, proto))
            return false;

        WorldSession* session = player->GetSession();
        uint32 const accountId = session->GetAccountId();
        if (!sTransmogrification->AddCollectedAppearance(accountId, proto->ItemId))
            return false;

        CharacterDatabase.Execute("INSERT IGNORE INTO custom_unlocked_appearances (account_id, item_template_id) VALUES ({}, {})",
            accountId, proto->ItemId);

        // Characters being created store their starting gear before they're in the world.
        if (announce && config.announce && player->IsInWorld() && !sTransmogrification->CanNeverTransmog(proto)
            && !player->GetPlayerSetting("mod-transmog", SETTING_HIDE_TRANSMOG).value)
            ChatHandler(session).PSendSysMessage("{} has been added to your appearance collection.", ItemLink(session, proto));

        return true;
    }

    bool Collect(Player* player, Item* item, bool announce)
    {
        return item && Collect(player, item->GetTemplate(), announce);
    }

    uint32 CollectBag(Player* player, uint8 bagPos)
    {
        uint32 added = 0;
        if (Bag* bag = player->GetBagByPos(bagPos))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                added += Collect(player, bag->GetItemByPos(slot), false);
        return added;
    }

    uint32 CollectInventoryAndBank(Player* player)
    {
        uint32 added = 0;

        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            added += Collect(player, player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot), false);
        for (uint8 bagPos = INVENTORY_SLOT_BAG_START; bagPos < INVENTORY_SLOT_BAG_END; ++bagPos)
            added += CollectBag(player, bagPos);

        for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; ++slot)
            added += Collect(player, player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot), false);
        for (uint8 bagPos = BANK_SLOT_BAG_START; bagPos < BANK_SLOT_BAG_END; ++bagPos)
            added += CollectBag(player, bagPos);

        return added;
    }
}

class TransmogCollectWorldScript : public WorldScript
{
public:
    TransmogCollectWorldScript() : WorldScript("TransmogCollectWorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        config.enabled      = sConfigMgr->GetOption<bool>("TransmogCollect.Enable", true);
        config.onPickup     = sConfigMgr->GetOption<bool>("TransmogCollect.OnPickup", true);
        config.onDisenchant = sConfigMgr->GetOption<bool>("TransmogCollect.OnDisenchant", true);
        config.scanOnLogin  = sConfigMgr->GetOption<bool>("TransmogCollect.ScanOnLogin", true);
        config.altBots      = sConfigMgr->GetOption<bool>("TransmogCollect.AltBots", true);
        config.announce     = sConfigMgr->GetOption<bool>("TransmogCollect.Announce", true);

        LOG_INFO("server.loading", "mod-transmog-collect: {} (pickup {}, disenchant {}, login scan {})",
            config.enabled ? "enabled" : "disabled", config.onPickup, config.onDisenchant, config.scanOnLogin);
    }
};

class TransmogCollectPlayerScript : public PlayerScript
{
public:
    TransmogCollectPlayerScript() : PlayerScript("TransmogCollectPlayerScript") { }

    // Every newly created item: loot, vendor, quest reward, crafting, starting gear.
    void OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/) override
    {
        if (config.onPickup && CanCollect(player))
            Collect(player, item, true);
    }

    // Existing items changing hands: trade, mail (including the auction house), guild bank.
    void OnPlayerAfterMoveItemToInventory(Player* player, Item* item, bool /*update*/) override
    {
        if (config.onPickup && CanCollect(player))
            Collect(player, item, true);
    }

    // Runs as the cast goes off, while the item is still there to read.
    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!config.onDisenchant || !spell || !spell->GetSpellInfo()->HasEffect(SPELL_EFFECT_DISENCHANT))
            return;

        if (CanCollect(player))
            Collect(player, spell->m_targets.GetItemTarget(), true);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!config.scanOnLogin || !CanCollect(player))
            return;

        if (uint32 const added = CollectInventoryAndBank(player); added && config.announce
            && !player->GetPlayerSetting("mod-transmog", SETTING_HIDE_TRANSMOG).value)
            ChatHandler(player->GetSession()).PSendSysMessage("{} appearance{} from your bags and bank added to your collection.",
                added, added == 1 ? "" : "s");
    }
};

void AddTransmogCollectScripts()
{
    new TransmogCollectWorldScript();
    new TransmogCollectPlayerScript();
}
