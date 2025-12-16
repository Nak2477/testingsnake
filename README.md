Priority 1 (Polish):

Add error messages to session browser UI
Implement countdown timer rendering
Add bounds check in session selection
Priority 2 (Robustness):
4. Better host detection from server
5. Collision-free respawn
6. Connection timeout handling

Priority 3 (Features):
7. Scoreboard display in MATCH_END
8. Reconnect after disconnect
9. Session filtering by identifier (HardcoreSnake only)

Priority 4 (Optimization):
10. Incremental collision map updates
11. Client-side prediction/interpolation
12. Delta compression for state sync

2. Magic Numbers ⚠️
Issue: Hardcoded values scattered throughout
Examples:

33 ms throttle (multiplayer.cpp:560)
5000 ms state sync interval
30 updates/sec
1000 food spawn attempts
Recommendation: Move to constants in header: