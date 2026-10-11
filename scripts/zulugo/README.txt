ZULU GO 2.0
===========

The Zulu feature set built on the GeneralsOnline client, playing on its own
server. It installs beside your normal game rather than on top of it, so your
regular GeneralsOnline play is unaffected: no file in the game folder is
replaced, and this build keeps its own login file, so signing in here does not
sign you out there.

WHAT YOU NEED
-------------
Command and Conquer Generals Zero Hour 1.04 with GeneralsOnline installed. The
installer finds that folder on its own and puts everything in a ZuluGO subfolder
inside it.

PLAY
----
Start it from the "Zulu GO" shortcut in the Start Menu or on the desktop. Use
the shortcut rather than the game exe: the shortcut runs the launcher, which
checks for an update and then starts the game with the settings it needs.

UPDATES
-------
The launcher checks for a new version each time you start, and installs one if
it finds it, which takes a few seconds. Everyone in a game has to be on the same
version, so letting it update is the easy way to stay playable. If you are
offline or the check fails, it starts the game you already have.

If you click ONLINE on a build the server no longer accepts, the game says so
and offers to close and run the updater for you. Only Zulu GO is updated by
this; your normal GeneralsOnline install is never touched.

SIGNING IN
----------
Click ONLINE. A browser page opens asking for a player name and a passphrase.

    Passphrase: zulu-test

Pick a name and keep using it. That is the name other players see and the name
your match stats are filed under. Then go back to the game and it signs you in
within a second or two.

WHAT IS NEW COMPARED TO YOUR NORMAL ZULU BUILD
----------------------------------------------
Online play runs through the GeneralsOnline service instead of the Zulu
coordinator. Everything else is the Zulu feature set you know, plus:

  Randomize          Host button in the lobby, rolls faction, colour and start
                     position for every slot nobody has chosen.
  Tactical AI        A slot type in the lobby dropdown, next to Hard and the
                     rest.
  Resume from replay Type /resume in a lobby to continue a match from its
                     replay. The host's replay is sent to everyone.
  Live spectating    The Live button in the custom lobby lists matches in
                     progress and lets you watch one behind a 30 second delay.
  Observer bar       Defeated players get a full viewer bar, with other
                     players' production queues and a hunted clock.

Match stats and replays go to cncstats and radarvan exactly as they do from
your normal Zulu build.

KNOWN ROUGH EDGES
-----------------
  - The observer bar has no background art yet. The minimap, buttons and stats
    table all work, the panel behind them is just black.
  - There is no relay server behind this yet. Behind a strict or carrier grade
    NAT you may fail to connect to some players where the Zulu coordinator
    would have relayed you.
  - Leaderboards and player stats on this server start empty, so everyone shows
    as unranked.

IF SOMETHING GOES WRONG
-----------------------
Send Bill the GeneralsOnline.log and ReleaseLog.txt from
    Documents\Command and Conquer Generals Zero Hour Data\GeneralsOnlineData
and anything in
    Documents\Command and Conquer Generals Zero Hour Data\GeneralsOnlineCrashData
along with roughly when it happened.

TO REMOVE IT
------------
Uninstall "Zulu GO" from Add or Remove Programs. It takes away the ZuluGO
folder, the shortcuts and the firewall rule it added, and leaves the rest of
your install alone.
