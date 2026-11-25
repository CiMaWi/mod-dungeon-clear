/*
 * mod-dungeon-clear — StayDeadAction.cpp
 */

#include "StayDeadAction.h"

#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Timer.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/DcRunState.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

namespace
{
    // BODY_HOLD_SECONDS (awareness AW29, a tuning value): the longest a body waits for a resurrection, counted from
    // the first call that saw it dead. Longer than the module's recovery timeout in a 5-man (PostCombatRezTimeoutSecs,
    // 90 s out of combat), so that recovery ends first. A raid's budget (180 s, plus 30 s per further body up to
    // 360 s, combat not counted) can outlast the hold: a body not raised by then releases.
    constexpr uint32 BODY_HOLD_SECONDS = 300;
    // The "often" trigger asks every few seconds while the bot is dead; a longer silence means it lived in between
    // (or was logged out), so the next call starts a new death's clock
    constexpr uint32 NEW_DEATH_GAP_MS = 60 * IN_MILLISECONDS;

    // The classes that can raise a body: mod-dungeon-clear's rez classes (DcRezRecovery's IsRezClass); playerbots'
    // party resurrection raises with the same spells in any group
    bool CanRaise(Player const* p)
    {
        switch (p->getClass())
        {
            case CLASS_PRIEST:
            case CLASS_PALADIN:
            case CLASS_SHAMAN:
            case CLASS_DRUID:
                return true;
            default:
                return false;
        }
    }

    // A living member of the bot's group in the bot's own instance (the same map object, not only the same map id)
    // that can raise it. A walk of the group, no allocation
    bool RaiserBeside(Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        Map const* map = bot->GetMap();
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != bot && member->IsInWorld() && member->GetMap() == map && member->IsAlive() &&
                CanRaise(member))
                return true;
        }
        return false;
    }

    // A raid regroup can still come for the bot's run: mod-dungeon-clear's run of the group is live (enabled, not
    // paused, its recovery on) on a map whose wipe regroups at the entrance (DcRezRecovery: a raid map or the
    // Oculus). Read only: DcRezRecovery::Evaluate stamps the recovery's clock and is the trigger's to call. When the
    // run gives up it disables itself, which ends this hold
    bool RegroupCanCome(Player* bot)
    {
        Map const* map = bot->GetMap();
        if (!map->IsRaid() && bot->GetMapId() != DcOculus::MAP_ID)
            return false;
        if (!DcRezRecovery::Enabled(bot))
            return false;
        Player* owner = DcLeaderSignal::FindRunOwner(bot);
        PlayerbotAI* ownerAI = owner ? GET_PLAYERBOT_AI(owner) : nullptr;
        if (!ownerAI)
            return false;
        DcRunState const& run = DcRun::Of(ownerAI);
        return run.enabled && !run.paused;
    }
}  // namespace

bool DungeonClearStayDeadAction::isUseful()
{
    // Dungeon and raid maps only: a bot dead in the open world keeps stock auto-release. The hold can't be gated on
    // an active run alone: a party death auto-disables a 5-man run (DungeonClearPartyDiedTrigger), so by the time
    // bots are bodies `enabled` is often false already. Read live (not cached) so `.reload config` and per-run
    // overrides take effect without a restart; the "often" cadence makes the lookup negligible.
    Map const* map = bot ? bot->GetMap() : nullptr;
    if (!map || !map->IsDungeon() || !DcSettings::GetBool(bot, "PreventBotRelease"))
        return AutoReleaseSpiritAction::isUseful();

    // Alive or a ghost: nothing to hold (the stock answer is no)
    if (!bot->isDead() || bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        return AutoReleaseSpiritAction::isUseful();

    // The body's clock: the first call that sees it dead stands for the death
    uint32 const now = getMSTime();
    if (!bodySeen || getMSTimeDiff(lastCallMs, now) > NEW_DEATH_GAP_MS)
    {
        bodySeen = true;
        bodySinceMs = now;
    }
    lastCallMs = now;

    // Held only while its group can still end the hold, and never past the bound; else the stock release, which
    // keeps a real player's bot waiting for its player
    if (getMSTimeDiff(bodySinceMs, now) < BODY_HOLD_SECONDS * IN_MILLISECONDS &&
        (RaiserBeside(bot) || RegroupCanCome(bot)))
        return false;

    return AutoReleaseSpiritAction::isUseful();
}
