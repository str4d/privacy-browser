/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1999 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#include "msgCore.h"  // for pre-compiled headers

#include "nsImapCore.h"
#include "nsImapFlagAndUidState.h"

/* amount to expand for imap entry flags when we need more */
const PRInt32 kFlagEntrySize =	100;

nsImapFlagAndUidState::nsImapFlagAndUidState(PRInt32 numberOfMessages, PRUint16 flags)
{
	fNumberOfMessagesAdded = 0;
	fNumberOfMessageSlotsAllocated = numberOfMessages;
	if (!fNumberOfMessageSlotsAllocated)
		fNumberOfMessageSlotsAllocated = kFlagEntrySize;
	fFlags = (imapMessageFlagsType*) PR_Malloc(sizeof(imapMessageFlagsType) * fNumberOfMessageSlotsAllocated); // new imapMessageFlagsType[fNumberOfMessageSlotsAllocated];
	
	fUids.SetSize(fNumberOfMessageSlotsAllocated);
	nsCRT::memset(fFlags, 0, sizeof(imapMessageFlagsType) * fNumberOfMessageSlotsAllocated);
	fSupportedUserFlags = flags;
	fNumberDeleted = 0;
}

nsImapFlagAndUidState::nsImapFlagAndUidState(const nsImapFlagAndUidState& state, 
										   uint16 flags)
{
	fNumberOfMessagesAdded = state.fNumberOfMessagesAdded;
	
	fNumberOfMessageSlotsAllocated = state.fNumberOfMessageSlotsAllocated;
	fFlags = (imapMessageFlagsType*) PR_Malloc(sizeof(imapMessageFlagsType) * fNumberOfMessageSlotsAllocated); // new imapMessageFlagsType[fNumberOfMessageSlotsAllocated];
	fUids.CopyArray((nsMsgKeyArray *) &state.fUids);

	memcpy(fFlags, state.fFlags, sizeof(imapMessageFlagsType) * fNumberOfMessageSlotsAllocated);
	fSupportedUserFlags = flags;
	fNumberDeleted = 0;
}

nsImapFlagAndUidState::~nsImapFlagAndUidState()
{
	PR_FREEIF(fFlags);
}
	
void
nsImapFlagAndUidState::SetSupportedUserFlags(uint16 flags)
{
	fSupportedUserFlags |= flags;
}


// we need to reset our flags, (re-read all) but chances are the memory allocation needed will be
// very close to what we were already using

void nsImapFlagAndUidState::Reset(PRUint32 howManyLeft)
{
	if (!howManyLeft)
		fNumberOfMessagesAdded = fNumberDeleted = 0;		// used space is still here
}


// Remove (expunge) a message from our array, since now it is gone for good

void nsImapFlagAndUidState::ExpungeByIndex(PRUint32 index)
{
	PRUint32 counter = 0;
	
	if ((PRUint32) fNumberOfMessagesAdded < index)
		return;
	index--;
	fNumberOfMessagesAdded--;
	if (fFlags[index] & kImapMsgDeletedFlag)	// see if we already had counted this one as deleted
		fNumberDeleted--;
	for (counter = index; counter < (PRUint32) fNumberOfMessagesAdded; counter++)
	{
		fUids.SetAt(counter, fUids[counter + 1]);
		fFlags[counter] = fFlags[counter + 1];
	}
}


	// adds to sorted list.  protects against duplicates and going past fNumberOfMessageSlotsAllocated  
void nsImapFlagAndUidState::AddUidFlagPair(PRUint32 uid, imapMessageFlagsType flags)
{
	// make sure there is room for this pair
	if (fNumberOfMessagesAdded >= fNumberOfMessageSlotsAllocated)
	{
		fNumberOfMessageSlotsAllocated += kFlagEntrySize;
		fUids.SetSize(fNumberOfMessageSlotsAllocated);
		fFlags = (imapMessageFlagsType*) PR_REALLOC(fFlags, sizeof(imapMessageFlagsType) * fNumberOfMessageSlotsAllocated); // new imapMessageFlagsType[fNumberOfMessageSlotsAllocated];
	}

	// optimize the common case of placing on the end
	if (!fNumberOfMessagesAdded || (uid > (PRInt32) fUids[fNumberOfMessagesAdded - 1]))
	{	
		fUids.SetAt(fNumberOfMessagesAdded, uid);
		fFlags[fNumberOfMessagesAdded] = flags;
		fNumberOfMessagesAdded++;
		if (flags & kImapMsgDeletedFlag)
			fNumberDeleted++;
		return;
	}
	
	// search for the slot for this uid-flag pair

	PRInt32 insertionIndex = -1;
	XP_Bool foundIt = FALSE;

	GetMessageFlagsFromUID(uid, &foundIt, &insertionIndex);

	// Hmmm, is the server sending back unordered fetch responses?
	if (((PRInt32) fUids[insertionIndex]) != uid)
	{
		// shift the uids and flags to the right
		for (PRInt32 shiftIndex = fNumberOfMessagesAdded; shiftIndex > insertionIndex; shiftIndex--)
		{
			fUids.SetAt(shiftIndex, fUids[shiftIndex - 1]);
			fFlags[shiftIndex] = fFlags[shiftIndex - 1];
		}
		fFlags[insertionIndex] = flags;
		fUids.SetAt(insertionIndex, uid);
		fNumberOfMessagesAdded++;
		if (fFlags[insertionIndex] & kImapMsgDeletedFlag)
			fNumberDeleted++;
	} else {
		if ((fFlags[insertionIndex] & kImapMsgDeletedFlag) && !(flags & kImapMsgDeletedFlag))
			fNumberDeleted--;
		else
		if (!(fFlags[insertionIndex] & kImapMsgDeletedFlag) && (flags & kImapMsgDeletedFlag))
			fNumberDeleted++;
		fFlags[insertionIndex] = flags;
	}
}


PRInt32 nsImapFlagAndUidState::GetNumberOfMessages()
{
	return fNumberOfMessagesAdded;
}
	
PRInt32 nsImapFlagAndUidState::GetNumberOfDeletedMessages()
{
	return fNumberDeleted;
}
	
// since the uids are sorted, start from the back (rb)

PRUint32  nsImapFlagAndUidState::GetHighestNonDeletedUID()
{
	PRUint32 index = fNumberOfMessagesAdded;
	do {
		if (index <= 0)
			return(0);
		index--;
		if (fUids[index] && !(fFlags[index] & kImapMsgDeletedFlag))
			return fUids[index];
	} while (index > 0);
	return 0;
}


// Has the user read the last message here ? Used when we first open the inbox to see if there
// really is new mail there.

XP_Bool nsImapFlagAndUidState::IsLastMessageUnseen()
{
	PRUint32 index = fNumberOfMessagesAdded;

	if (index <= 0)
		return FALSE;
	index--;
	// if last message is deleted, it was probably filtered the last time around
	if (fUids[index] && (fFlags[index] & (kImapMsgSeenFlag | kImapMsgDeletedFlag)))
		return FALSE;
	return TRUE; 
}


PRUint32 nsImapFlagAndUidState::GetUidOfMessage(PRInt32 zeroBasedIndex)
{
	if (zeroBasedIndex < fNumberOfMessagesAdded)
		return fUids[zeroBasedIndex];
	return (-1);	// so that value is non-zero and we don't ask for bad msgs
}


// find a message flag given a key with non-recursive binary search, since some folders
// may have thousand of messages, once we find the key set its index, or the index of
// where the key should be inserted

imapMessageFlagsType nsImapFlagAndUidState::GetMessageFlagsFromUID(PRUint32 uid, XP_Bool *foundIt, PRInt32 *ndx)
{
	PRInt32 index = 0;
	PRInt32 hi = fNumberOfMessagesAdded - 1;
	PRInt32 lo = 0;

	*foundIt = FALSE;
	*ndx = -1;
	while (lo <= hi)
	{
		index = (lo + hi) / 2;
		if (fUids[index] == (PRUint32) uid)
		{
			PRInt32 returnFlags = fFlags[index];
			
			*foundIt = TRUE;
			*ndx = index;
			return returnFlags;
		}
		if (fUids[index] > (PRUint32) uid)
			hi = index -1;
		else if (fUids[index] < (PRUint32) uid)
			lo = index + 1;
	}
	index = lo;
	while ((index > 0) && (fUids[index] > (PRUint32) uid))
		index--;
	if (index < 0)
		index = 0;
	*ndx = index;
	return 0;
}



imapMessageFlagsType nsImapFlagAndUidState::GetMessageFlags(PRInt32 zeroBasedIndex)
{
	imapMessageFlagsType returnFlags = kNoImapMsgFlag;
	if (zeroBasedIndex < fNumberOfMessagesAdded)
		returnFlags = fFlags[zeroBasedIndex];

	return returnFlags;
}

extern "C" {
nsImapFlagAndUidState *IMAP_CreateFlagState(PRInt32 numberOfMessages)
{
	return new nsImapFlagAndUidState(numberOfMessages);
}


void IMAP_DeleteFlagState(nsImapFlagAndUidState *state)
{
	delete state;
}


PRInt32 IMAP_GetFlagStateNumberOfMessages(nsImapFlagAndUidState *state)
{
	return state->GetNumberOfMessages();
}
	

PRUint32 IMAP_GetUidOfMessage(PRInt32 zeroBasedIndex, nsImapFlagAndUidState *state)
{
	return state->GetUidOfMessage(zeroBasedIndex);
}


imapMessageFlagsType IMAP_GetMessageFlags(PRInt32 zeroBasedIndex, nsImapFlagAndUidState *state)
{
	return state->GetMessageFlags(zeroBasedIndex);
}


imapMessageFlagsType IMAP_GetMessageFlagsFromUID(PRUint32 uid, XP_Bool *foundIt, nsImapFlagAndUidState *state)
{
	PRInt32 index = -1;
	return state->GetMessageFlagsFromUID(uid, foundIt, &index);
}

XP_Bool IMAP_SupportMessageFlags(nsImapFlagAndUidState *state,
								 imapMessageFlagsType flags)
{
	return (state->GetSupportedUserFlags() & flags);
}


}	// extern "C"

