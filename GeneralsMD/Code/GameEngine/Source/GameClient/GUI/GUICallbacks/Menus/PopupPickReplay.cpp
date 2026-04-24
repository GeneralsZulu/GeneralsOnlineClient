/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// FILE: PopupPickReplay.cpp ///////////////////////////////////////////////////////////////////////
// Popup listbox that lets the LAN host pick a replay to resume from. Validates the
// lobby roster against the replay's roster by display name and reports whether the
// selection is valid. The Arm path that stashes resume state on the LANGameInfo
// and broadcasts it to peers is a TODO until the engine-side catch-up and handoff
// pieces land.

#include "PreRTS.h"

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "Common/Recorder.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/ReplayMenu.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/LANGameInfo.h"
#include "GameNetwork/GameInfo.h"

static const Int HANDOFF_FRAMES_BEFORE_END = 600; // 10 seconds at 30 logic fps.

static NameKeyType parentID = NAMEKEY_INVALID;
static NameKeyType listboxID = NAMEKEY_INVALID;
static NameKeyType staticTextStatusID = NAMEKEY_INVALID;
static NameKeyType buttonArmID = NAMEKEY_INVALID;
static NameKeyType buttonCancelID = NAMEKEY_INVALID;

static GameWindow* parentWin = nullptr;
static GameWindow* listboxWin = nullptr;
static GameWindow* staticTextStatusWin = nullptr;
static GameWindow* buttonArmWin = nullptr;
static GameWindow* buttonCancelWin = nullptr;
static WindowLayout* popupLayout = nullptr;

static Bool selectionIsValid = FALSE;

// Ground-truth filename is always pulled from the listbox at call time to
// avoid a file-scope AsciiString. ZH's memory-pool system is not up when
// global static constructors run, so a non-POD static here can crash during
// CRT init (right after the EA logo video).
static AsciiString currentSelectedFilename()
{
	AsciiString out;
	if (!listboxWin)
		return out;
	Int sel = -1;
	GadgetListBoxGetSelected(listboxWin, &sel);
	if (sel < 0)
		return out;
	UnicodeString fnameU = GetReplayFilenameFromListbox(listboxWin, sel);
	out.translate(fnameU);
	return out;
}

static void closePopup()
{
	if (popupLayout)
	{
		popupLayout->destroyWindows();
		deleteInstance(popupLayout);
		popupLayout = nullptr;
	}
	parentWin = nullptr;
	listboxWin = nullptr;
	staticTextStatusWin = nullptr;
	buttonArmWin = nullptr;
	buttonCancelWin = nullptr;
	selectionIsValid = FALSE;
}

static void setStatus(const UnicodeString& msg, Bool valid)
{
	if (staticTextStatusWin)
		GadgetStaticTextSetText(staticTextStatusWin, msg);
	if (buttonArmWin)
		buttonArmWin->winEnable(valid ? TRUE : FALSE);
	selectionIsValid = valid;
}

// Match lobby humans to replay humans by display name. Returns TRUE if every
// replay human has a counterpart in the lobby and vice versa. The actual slot
// reorder + resume-state stash lands with the engine-side plumbing and is left
// as a TODO here.
static Bool validateRoster(const ReplayGameInfo& replayInfo, UnicodeString& statusOut)
{
	LANGameInfo* myGame = TheLAN ? TheLAN->GetMyGame() : nullptr;
	if (!myGame)
	{
		statusOut = TheGameText->fetch("GUI:ResumeNoLobby");
		return FALSE;
	}

	// VC6's 'for' scoping leaks the loop variable into the enclosing scope, so a
	// single 'i' is shared across loops here.
	Int i;

	UnicodeString replayNames[MAX_SLOTS];
	Int replayCount = 0;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot* s = replayInfo.getConstSlot(i);
		if (s && s->isHuman())
			replayNames[replayCount++] = s->getName();
	}

	UnicodeString lobbyNames[MAX_SLOTS];
	Int lobbyCount = 0;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot* s = myGame->getConstSlot(i);
		if (s && s->isHuman())
			lobbyNames[lobbyCount++] = s->getName();
	}

	if (replayCount != lobbyCount)
	{
		statusOut.format(TheGameText->fetch("GUI:ResumeCountMismatch"), lobbyCount, replayCount);
		return FALSE;
	}

	for (i = 0; i < replayCount; ++i)
	{
		Bool found = FALSE;
		for (Int j = 0; j < lobbyCount; ++j)
		{
			if (replayNames[i].compare(lobbyNames[j]) == 0)
			{
				found = TRUE;
				break;
			}
		}
		if (!found)
		{
			statusOut.format(TheGameText->fetch("GUI:ResumeMissingPlayer"), replayNames[i].str());
			return FALSE;
		}
	}

	statusOut = TheGameText->fetch("GUI:ResumeReady");
	return TRUE;
}

static void onListboxSelectionChanged()
{
	if (!listboxWin)
		return;

	Int sel = -1;
	GadgetListBoxGetSelected(listboxWin, &sel);
	if (sel < 0)
	{
		setStatus(TheGameText->fetch("GUI:ResumePickerPrompt"), FALSE);
		return;
	}

	AsciiString fname = currentSelectedFilename();
	if (fname.isEmpty())
	{
		setStatus(TheGameText->fetch("GUI:ResumePickerPrompt"), FALSE);
		return;
	}

	RecorderClass::ReplayHeader header;
	ReplayGameInfo info;
	const MapMetaData* mapData = nullptr;
	if (!readReplayMapInfo(fname, header, info, mapData))
	{
		setStatus(TheGameText->fetch("GUI:ResumeHeaderUnreadable"), FALSE);
		return;
	}
	if (!RecorderClass::replayMatchesGameVersion(header))
	{
		setStatus(TheGameText->fetch("GUI:ResumeVersionMismatch"), FALSE);
		return;
	}
	if (header.frameCount < (UnsignedInt)HANDOFF_FRAMES_BEFORE_END)
	{
		setStatus(TheGameText->fetch("GUI:ResumeTooShort"), FALSE);
		return;
	}

	UnicodeString status;
	Bool ok = validateRoster(info, status);
	setStatus(status, ok);
}

static void onArmPressed()
{
	AsciiString fname = currentSelectedFilename();
	if (!selectionIsValid || fname.isEmpty())
		return;

	// TODO(bill-rich): stash fname + handoff frame on TheLAN->GetMyGame()
	// and broadcast updated game options so all clients arm resume mode. Requires
	// the host streamer, network seed override, MSG_REPLAY_HANDOFF wire message,
	// and input-suppression gate (tasks 6-11 in the plan) to be in place first.
	if (staticTextStatusWin)
		GadgetStaticTextSetText(staticTextStatusWin, TheGameText->fetch("GUI:ResumeEngineTODO"));
}

// -----------------------------------------------------------------------------
// Window callbacks referenced from PopupPickReplay.wnd
// -----------------------------------------------------------------------------

void PopupPickReplayInit(WindowLayout* layout, void* /*userData*/)
{
	popupLayout = layout;

	parentID            = TheNameKeyGenerator->nameToKey("PopupPickReplay.wnd:PopupPickReplayParent");
	listboxID           = TheNameKeyGenerator->nameToKey("PopupPickReplay.wnd:ListboxReplayFiles");
	staticTextStatusID  = TheNameKeyGenerator->nameToKey("PopupPickReplay.wnd:StaticTextStatus");
	buttonArmID         = TheNameKeyGenerator->nameToKey("PopupPickReplay.wnd:ButtonArm");
	buttonCancelID      = TheNameKeyGenerator->nameToKey("PopupPickReplay.wnd:ButtonCancel");

	parentWin           = TheWindowManager->winGetWindowFromId(nullptr, parentID);
	listboxWin          = TheWindowManager->winGetWindowFromId(parentWin, listboxID);
	staticTextStatusWin = TheWindowManager->winGetWindowFromId(parentWin, staticTextStatusID);
	buttonArmWin        = TheWindowManager->winGetWindowFromId(parentWin, buttonArmID);
	buttonCancelWin     = TheWindowManager->winGetWindowFromId(parentWin, buttonCancelID);

	PopulateReplayFileListbox(listboxWin);
	if (buttonArmWin)
		buttonArmWin->winEnable(FALSE);
	if (staticTextStatusWin)
		GadgetStaticTextSetText(staticTextStatusWin, TheGameText->fetch("GUI:ResumePickerPrompt"));

	TheWindowManager->winSetFocus(parentWin);
	onListboxSelectionChanged();
}

void PopupPickReplayUpdate(WindowLayout* /*layout*/, void* /*userData*/)
{
}

void PopupPickReplayShutdown(WindowLayout* /*layout*/, void* /*userData*/)
{
}

WindowMsgHandledType PopupPickReplaySystem(GameWindow* /*window*/, UnsignedInt msg,
	WindowMsgData mData1, WindowMsgData /*mData2*/)
{
	switch (msg)
	{
		case GWM_CREATE:
		case GWM_DESTROY:
			return MSG_HANDLED;

		case GWM_INPUT_FOCUS:
			*(Bool*)mData1 = TRUE;
			return MSG_HANDLED;

		case GLM_SELECTED:
			onListboxSelectionChanged();
			return MSG_HANDLED;

		case GLM_DOUBLE_CLICKED:
			onListboxSelectionChanged();
			if (selectionIsValid)
			{
				onArmPressed();
			}
			return MSG_HANDLED;

		case GBM_SELECTED:
		{
			GameWindow* control = (GameWindow*)mData1;
			Int controlID = control->winGetWindowId();
			if (controlID == buttonCancelID)
			{
				closePopup();
			}
			else if (controlID == buttonArmID)
			{
				onArmPressed();
				// closePopup() intentionally omitted so user can see the TODO
				// status message. Flip to closePopup() once engine path lands.
			}
			return MSG_HANDLED;
		}

		default:
			return MSG_IGNORED;
	}
}
