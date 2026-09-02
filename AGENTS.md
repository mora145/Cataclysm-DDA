\# CDDA AI Project



\## Mission



This is a modified Cataclysm: Dark Days Ahead repository.



The project already contains custom NPC AI work.

DO NOT assume this is vanilla CDDA.



Before designing or implementing anything, understand the existing custom code.



Important existing custom files include:



\- src/npc\_ai\_client.cpp

\- src/npc\_ai\_context.cpp

\- src/npc\_ai\_wield.cpp

\- src/npc\_ai\_batch\_pickup.cpp

\- src/npc\_ai\_batch\_pickup.h

\- src/npc\_ai\_spontaneous.cpp

\- src/npc\_ai\_spontaneous.h



Existing AI-related modifications also exist in:



\- src/npc.cpp

\- src/npcmove.cpp

\- src/npctalk.cpp

\- src/weather.h



Do not replace this architecture with a parallel AI system without first understanding why the existing code works the way it does.



\---



\# Long-term objective



Make CDDA NPCs feel alive and autonomous.



NPCs should eventually be able to:



\- perceive their environment

\- understand natural-language player requests

\- generate high-level goals

\- plan multi-step tasks

\- make decisions without constant player commands

\- use existing CDDA mechanics to execute those decisions

\- manage faction camps

\- organize items

\- unload vehicles

\- create storage/loot zones when appropriate

\- recognize suitable empty furniture

\- assign jobs to other NPCs

\- coordinate with other NPCs

\- react to danger

\- interrupt tasks when necessary

\- remember responsibilities and relevant player orders



The goal is NOT to create NPCs that cheat.



\---



\# Core architecture



Prefer:



Natural language / LLM

&#x20;       |

&#x20;       v

Structured high-level goal

&#x20;       |

&#x20;       v

Deterministic planner

&#x20;       |

&#x20;       v

Existing CDDA systems

&#x20;       |

&#x20;       v

Physical NPC actions



The LLM should decide WHAT the NPC wants to accomplish.



CDDA should determine HOW the physical actions occur.



Do not use the LLM to control individual movement turns.



Do not teleport characters or items.



Do not create resources magically.



NPCs must respect normal:



\- pathfinding

\- movement

\- carrying capacity

\- stamina

\- inventory capacity

\- skills

\- tools

\- time

\- danger

\- accessibility

\- game rules



\---



\# First major autonomy target



Eventually support this natural-language interaction:



Player:



"Unload the truck and organize everything."



The NPC should be capable of:



1\. identifying the intended vehicle

2\. inspecting vehicle cargo

3\. inspecting existing camp storage zones

4\. determining where cargo should go

5\. detecting missing storage categories

6\. locating suitable empty furniture or storage areas

7\. creating appropriate zones if necessary

8\. unloading the vehicle

9\. sorting items using normal CDDA activities

10\. asking another NPC for assistance when useful

11\. reporting blockers instead of cheating



This should become a reusable planning framework,

not a hard-coded special-case script.



\---



\# Existing CDDA systems to investigate



Always search the existing implementation before adding new functionality.



Important areas likely include:



\- src/npc.cpp

\- src/npc.h

\- src/npcmove.cpp

\- src/npctalk.cpp

\- src/faction\_camp.cpp

\- src/basecamp.cpp

\- src/activity\_item\_handling.cpp

\- src/activity\_item\_handling.h

\- src/clzones.cpp

\- src/clzones.h

\- src/zone\_manager\_ui.cpp

\- behavior trees

\- NPC activities

\- faction systems

\- faction camps

\- vehicle cargo

\- item categorization

\- loot zones

\- zone\_manager

\- overmap NPC handling



Relevant concepts known to exist include:



\- ACT\_MOVE\_LOOT

\- UNLOAD\_ALL

\- CAMP\_STORAGE

\- loot zones

\- vehicle zones

\- camp\_work

\- free\_time

\- order

\- duty



These are starting points, not assumptions about the final architecture.



\---



\# Development philosophy



Reuse existing CDDA systems whenever possible.



Prefer a small adapter or hook over rewriting a core subsystem.



Keep AI functionality modular.



Separate:



\- perception

\- memory/context

\- decision making

\- planning

\- task execution

\- natural-language communication



Physical actions should remain deterministic whenever possible.



LLM output should ideally become structured commands or goals.



Example:



goal = unload\_vehicle

vehicle = target\_vehicle

organize = true

create\_missing\_zones = true



The deterministic game-side planner then executes the goal.



\---



\# Existing project protection



The repository contains working custom AI code.



Before modifying existing AI code:



1\. inspect the relevant file

2\. determine how it connects to the rest of the project

3\. search for callers and dependencies

4\. explain the proposed integration

5\. avoid regressions



Do not silently remove existing AI behavior.



Do not rewrite working systems merely because another implementation looks cleaner.



Incremental evolution is preferred over replacement.



\---



\# Git safety



Do not run destructive Git commands.



Never automatically execute:



\- git reset --hard

\- git clean -fd

\- git checkout -- .

\- git restore .

\- mass deletion of files



Do not amend or destroy existing commits.



Do not push to GitHub unless explicitly requested.



Before modifying files inspect:



git status

git diff



Do not discard unrelated changes.



\---



\# Build and validation



This repository has successfully compiled on this Windows machine using Visual Studio 2022 / MSBuild.



When modifying C++:



1\. make focused changes

2\. compile the relevant target

3\. inspect compiler errors

4\. correct errors introduced by the change

5\. compile again

6\. run focused tests where practical



Do not alter tests simply to hide a regression.



\---



\# Current phase



CURRENT PHASE IS RESEARCH.



Do not implement the NPC camp autonomy system yet unless explicitly instructed.



First understand:



1\. existing custom npc\_ai\_\* architecture

2\. vanilla NPC decision architecture

3\. camp worker architecture

4\. ACT\_MOVE\_LOOT

5\. loot/storage zones

6\. vehicle cargo handling

7\. zone creation

8\. item categorization

9\. furniture/storage detection

10\. NPC-to-NPC task coordination



Then propose how the existing custom AI system can evolve to support autonomous camp management.



Prefer extending what already exists rather than introducing an unrelated second system.

