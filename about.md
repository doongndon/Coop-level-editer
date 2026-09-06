# Coop Level Editor

Build one level together with a friend, live. You place a block, they see it
appear. They pick a colour, your background changes.

## Getting in

**To host** — open your level, tap **EDIT**, then **COOP** on the bar that
appears, then **Create Room**. Your level becomes the room's level.

**To join** — from **My Levels**, tap **COOP** on the left, then pick the room
from the list. You get a temporary level to work in, and it is deleted when you
leave. Your own levels are never touched.

Both of you need the same version of the mod.

## While you work

The bar sits inside the **EDIT** tab. Drag it anywhere by the small white
handle on its left; where you leave it is remembered.

- **CHAT** — type to each other. English only: the game's font has no other
  alphabet, so anything else comes out blank.
- **GO TO** — jump your view to where your partner is looking.
- **RESYNC** — pull the room's level again if the two of you have drifted
  apart.

You can see your partner's cursor, and an orange outline around whatever they
have selected.

## What travels

Objects, and everything about them: position, rotation, scale, colour, groups,
trigger settings. Deleting travels too, and so does undo.

Background, ground, colours and the song are shared as well. Whoever changes
them last wins.

## Rooms

A room stays open while someone is in it. When the last person leaves, the room
and its level are gone. Set a password when you create a room if you want it
closed to others; the room list shows a `*` next to locked rooms.

## If something looks wrong

The bar shows a line of numbers. `obj 110/110` is how many objects your level
holds and how many are paired with the room. Those two should match, and so
should your partner's. If they don't, press **RESYNC**.

Large levels take a moment to arrive - the bar counts down while they do.

## Server

Rooms run through a small relay server. The default one is free and shared, so
it sleeps when nobody uses it and takes a few seconds to wake. You can point
the mod at your own in the settings; the server is in the source repository.
