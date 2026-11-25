/*
 * mod-dungeon-clear — StayDeadAction.h
 *
 * Overrides mod-playerbots' "auto release" action so a dead bot on a dungeon or
 * raid map keeps its body (does not release its spirit to the graveyard) while
 * its group can still raise it: a living member of its group in its instance of
 * a class that can resurrect (priest, paladin, shaman, druid), or a raid
 * regroup that can still come for its run (a live run of this module on a map
 * that regroups on a wipe). The hold is bounded: BODY_HOLD_SECONDS (300 s) after
 * the death at most, longer than the module's own recovery timeout in a 5-man
 * (a raid's grows with its bodies and can outlast it). Every other
 * body gets the stock answer (an ungrouped bot or one under a bot leader
 * releases at once; a real player's bot waits for its player), and playerbots'
 * rule of the dead brings the ghost back by the healer of the entrance's grave.
 * Outside instances stock auto-release always applies.
 *
 * Housed in this module (not in mod-playerbots) so the stock module stays
 * unedited and conflict-free on upstream pulls. The wiring is the same override
 * seam DungeonClear already uses: DungeonClearActionContext registers the
 * "auto release" creator name, and because the engine's shared creator map keeps
 * the LAST registration for a given name (SharedNamedObjectContextList::Add) and
 * the DungeonClear contexts are appended AFTER playerbots builds its own, this
 * creator wins for every bot of every class.
 *
 * Gated by the config flag DungeonClear.PreventBotRelease (default on). When the
 * flag is off — or the bot is not on a dungeon/raid map — this behaves exactly
 * like the stock AutoReleaseSpiritAction.
 *
 * NOTE: this only suppresses the routine spirit-release. The DeadStrategy's
 * anti-stuck "repop" paths (falling far / location stuck) are intentionally left
 * intact as a safety net against a dead bot getting wedged in geometry.
 */

#ifndef _DUNGEONCLEAR_STAYDEADACTION_H
#define _DUNGEONCLEAR_STAYDEADACTION_H

#include "ReleaseSpiritAction.h"

class PlayerbotAI;

class DungeonClearStayDeadAction : public AutoReleaseSpiritAction
{
public:
    DungeonClearStayDeadAction(PlayerbotAI* botAI)
        : AutoReleaseSpiritAction(botAI, "auto release") {}

    bool isUseful() override;

private:
    // The body's clock, per bot (one action object per bot, on its map thread): when the first call saw this body
    // dead and the last call; a gap of more than a minute between calls starts a new death
    bool bodySeen = false;
    uint32 bodySinceMs = 0;
    uint32 lastCallMs = 0;
};

#endif
