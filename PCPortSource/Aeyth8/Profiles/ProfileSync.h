#pragma once
#include <string>

/*

Written by Aeyth8

*/

// Phase 4: host to client MatchingPlayers snapshot synchronisation.
//
// The snapshot travels over APlayerController::ClientTeamMessage using a dedicated FName type
// so it never shows up in the chat UI. The wire format is exactly:
//
//   AJB_MP_SYNC|1|<SnapshotId>|<ChunkIndex>/<ChunkCount>|<Base64UrlPayload>
//
// Receiver side:
//   * only messages starting with AJB_MP_SYNC| and typed AJBProfileSync are consumed
//   * every other message is forwarded to the original implementation untouched

namespace A8CL
{
namespace AJB
{

	// Host side: serialises the authoritative player table and pushes it to every client.
	void SyncMatchingPlayersToClients();

	// Client side: reassembles chunks and applies a completed snapshot to the local
	// GameInstance. Safe to call with any message, non matching payloads are ignored.
	// Returns true when the message was consumed by the profile sync system.
	bool HandleMatchingPlayersSyncMessage(const std::string& Message, const std::string& TypeName);

	// Applies a fully reassembled snapshot JSON to the local MatchingPlayers table.
	bool ApplyMatchingPlayersSnapshot(const std::string& SnapshotJson);

	// Drops stale partial snapshots. Called from the tick so a lost chunk cannot leak forever.
	void TickMatchingPlayersSync(float DeltaSeconds);

	// Registers the dedicated FName used to tag sync messages.
	void InitMatchingPlayersSync();

	// The FName type the sync system uses, initialised by InitMatchingPlayersSync.
	const wchar_t* GetMatchingPlayersSyncTypeName();

	// True when a message is a profile sync payload based on its prefix.
	bool IsMatchingPlayersSyncMessage(const std::string& Message);

}
}