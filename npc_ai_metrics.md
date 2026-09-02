# NPC AI — Métricas de optimización

## BASE REAL DE CONVERSACIÓN — cierre (29/08/2026)

Checkpoint de partida: `adf1608531` (`checkpoint-ai-2026-08-27`), working
tree inicialmente limpio. El baseline inmediato del gate era
`[npc_ai] --rng-seed 1` = **147/147 casos, 2027 aserciones, PASS**.

Resultado funcional medido:

- DIRECT_PLAYER_DIALOGUE: una petición y un target; no autoriza
  NPC_TO_NPC_REPLY. Las réplicas sociales ya encoladas que sean anteriores a
  una nueva pregunta directa se suprimen al completarse.
- GROUP_PLAYER_DIALOGUE: una petición independiente por NPC elegible, con
  contexto y Self State propios; permite varias respuestas, pero no deriva una
  cadena social de la frase del jugador.
- SPONTANEOUS_WORLD_EVENT y NPC_INITIATED_SOCIAL conservan conversación
  NPC->NPC y profundidad máxima 1.
- Context Router: HEALTH, PERCEPTION, INVENTORY_EQUIPMENT y CURRENT_SITUATION.
  Fuentes físicas: HP actual/máximo vanilla por body part, dolor percibido,
  sangrado/efectos, `sees` real e inventario/equipo real.
- Prueba de transporte final: el prompt recibido exactamente por el executor y
  el JSON producido por `build_ollama_request_json` contienen `id=arm_r`, el HP
  real y `severidad_dano=grave` para un brazo derecho al 10 %.
- Dedupe previo: solo por NPC. No se encontró cache/candidata compartida; dos
  requests independientes podían producir el mismo texto. Red de seguridad
  nueva: 24 líneas globales, 600 turnos, coincidencia exacta/normalizada o casi
  idéntica con umbral estricto; frases distintas sobre el mismo evento pasan.
- Mensajes visibles auditados en `npc_ai_wield.cpp`, `npc_ai_equipment.cpp`,
  `npc_ai_pickup.cpp`, `npc_ai_batch_pickup.cpp` y `npcmove.cpp`. Los msgids
  propios ausentes de `es_ES` usan fallback español en runtime; no se modificó
  ni regeneró ningún `.po`.

Gates exactos:

- Build Release x64 tests: **PASS**.
- `[npc_ai_conversation] --rng-seed 1`: **27/27 casos, 425 aserciones, PASS**.
- Context/self/percepción/idioma/social focalizados: **29/29 casos, 401
  aserciones, PASS**.
- `[npc_ai_ollama],[npc_ai_equipment_regression] --rng-seed 1`: **15/15 casos,
  181 aserciones, PASS**.
- `[npc_ai] --rng-seed 1`: **157/157 casos, 2203 aserciones, PASS**.
- Comparación inmediata: **+10 casos, +176 aserciones, cero fallos nuevos**.
- Build Release x64 juego: **PASS**.
- Binario: `C:\CDDA-AI\Cataclysm-DDA\cataclysm-tiles.exe`, 44 532 224 bytes,
  versión `cdda-0.I-2026-06-06-1535-28-gadf1608531-dirty`.
- `git diff --check`: **PASS**; solo avisos esperados LF->CRLF.

La suite global no se repitió en este bloque: el protocolo vigente pidió focos
y gate `[npc_ai]`. La última corrida global autoritativa sigue siendo 1279 PASS
/ 12 FAIL; los 12 nombres históricos permanecen documentados en
`NPC_AI_HANDOFF.md`, sección 15, y no pertenecen a las rutas modificadas.

Estado de entrega: **LISTO PARA PRUEBA MANUAL DE CONVERSACIÓN**. No iniciar el
bloque grande de eventos de combate hasta recibir los logs nuevos del jugador.

## PICKUP CON FALLBACK A WIELD — gate rapido (29/08/2026)

Alcance: corregir `recoge la espada` cuando el candidato real no cabe en un
bolsillo pero si en las manos, hacer audible todo rechazo y aplicar la misma
validacion fisica a pickup general, recover y batch.

- Build Release x64 tests: **PASS**.
- `[npc_ai_equipment_regression] --rng-seed 1`: **12/12 casos, 117
  aserciones, PASS**.
- `[npc_ai] --rng-seed 1`: **147/147 casos, 2027 aserciones, PASS**.
- Build Release x64 juego (`cataclysm-tiles.exe`): **PASS**.
- Suite completa `--order decl --rng-seed 1`: **PENDIENTE** por prioridad
  explicita de prueba jugable. Debe compararse por nombre contra los 12 fallos
  congelados; el gate rapido no la sustituye.

Los tres accesos invalidos del primer gate pertenecian al nuevo fixture
`npc_ai_spanish_pickup_name_selects_the_model_chosen_real_candidate`: 30 items
residuales del avatar desplazaban espada/ramita fuera del limite, el fake
devolvia indice 0 y el test llamaba incondicionalmente a la ruta vanilla
`pick_up_item()` sin pickup dirigido. La ruta nueva de wield no se alcanzaba.
Tras limpiar los items despues de `clear_avatar`, exigir un indice real antes de
ejecutar y revalidar el `item_location` despues de cualquier suelta, el gate
agregado termino completo sin crash.

Estado de entrega: **PARCIAL** hasta prueba en partida y suite global.

Binario: `Cataclysm-test-vcpkg-static-Release-x64.exe`
Versión: `cdda-0.I-2026-06-06-1535-26-g2370f0d5fd-dirty` (HEAD = 2370f0d5fd)
Harness: `tests/npc_ai_baseline_bench.cpp`, tag `[.npc_ai_baseline]` (opt-in)
Probe previo: `npc_ai_performance_probe`, tag `[.npc_ai_performance]`

Comando de medición:

```
.\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai_baseline]" -d yes
.\Cataclysm-test-vcpkg-static-Release-x64.exe "npc_ai_performance_probe" -d yes
```

---

## BASELINE RAW — 28/08/2026, HEAD sin modificar

Estado: árbol en HEAD, con todo el logging de debug actual activo.
Único añadido: el archivo de test `npc_ai_baseline_bench.cpp` (aditivo, no toca
código de producción).

### R1. Coste de percepción por radio

`build_sensory_snapshot( who, radius )`, media de 10 repeticiones, mapa vacío.

Constantes del escaneo actual: `z_levels=21`, `tiles_per_z=17424`,
`candidates_per_call=365904`.

| radio | avg_us | tiles devueltos | cota sup. casillas en radio |
|---|---|---|---|
| 6 | 2 419 | 290 | 3 549 |
| 12 | 5 759 | 290 | 13 125 |
| 20 | 13 043 | 290 | 35 301 |
| 60 | 102 593 | 290 | 307 461 |

Observación: el número de casillas **relevantes** devueltas es constante (290)
mientras el coste crece 42× con el radio. El trabajo escala con el área
escaneada, no con la información producida. Coste ≈ 0,33 µs por casilla
candidata, que es entre 15× y 30× lo esperable para una consulta de terreno.

### R2. Coste de prompt por tipo de consulta

`build_npc_prompt( who, línea )`, media de 5 repeticiones, 1 NPC.

| tipo | consulta | avg_us | bytes | sensory | self | scene |
|---|---|---|---|---|---|---|
| greeting | "Hola." | 12 280 | 11 678 | 0 | 0 | 0 |
| self_state | "Como estas?" | 5 508 | 11 596 | 0 | 1 | 0 |
| injury | "Estas herido?" | 5 489 | 11 598 | 0 | 1 | 0 |
| scene_short | "Que ves?" | 102 292 | 21 559 | 1 | 0 | 1 |
| scene_detailed | "Describe todo lo que ves" | 102 725 | 21 575 | 1 | 0 | 1 |
| memory | "Donde dejamos el rifle?" | 9 581 | 11 696 | 0 | 0 | 0 |

Observación: "Que ves?" cuesta **102 ms en el hilo principal**, un tirón
perceptible. Un simple saludo o un "¿cómo estás?" ya genera un prompt de
**11,6 KB**, la mayor parte irrelevante para la pregunta (base de la Aclaración 7).

### R3. Escalado de prompt grupal

`build_npc_prompt( ally, "Como estan?" )` para cada NPC del grupo.

| NPC | total_us | us/NPC | bytes totales | bytes/NPC |
|---|---|---|---|---|
| 1 | 8 614 | 8 614 | 11 684 | 11 684 |
| 5 | 45 206 | 9 041 | 101 162 | 20 232 |
| 10 | 91 923 | 9 192 | 345 545 | 34 554 |
| 20 | 207 455 | 10 372 | 672 683 | 33 634 |

Observación: los bytes crecen de forma **superlineal** (O(N²)): cada prompt
describe a los demás NPC. Con 20 NPC, una sola frase del jugador produce
**672 KB** de prompt enviados a Ollama y 207 ms de bloqueo en el hilo principal.

### R4. Escalado del hot path por turno

100 turnos de `regen_ai_cache` + `process_combat_social` +
`process_spontaneous_speech` + `process_ai_completions`, sin combate.

| NPC | us/turno | us/NPC/turno | combat_snapshot avg_us |
|---|---|---|---|
| 1 | 18 | 18 | 4,55 |
| 5 | 100 | 20 | 34,21 |
| 10 | 295 | 29 | 76,91 |
| 20 | 956 | 47 | 160,66 |

Observación: el coste **por NPC** crece de 18 a 47 µs, es decir el total es
O(N²). El origen es `build_combat_perception_snapshot`, cuyo coste medio crece
linealmente con la población (4,55 → 160,66 µs). Con 20 NPC son ~1 ms por turno
sólo en observación, en un mapa vacío y sin combate.

### R5. Requests LLM por evento compartido

Un único zombi visible para todos los observadores.

| observadores | requests LLM encolados | profundidad de cola | us |
|---|---|---|---|
| 1 | 1 | 1 | 2 712 |
| 5 | 5 | 5 | 12 809 |
| 10 | 10 | 10 | 25 579 |
| 20 | 20 | 20 | 56 644 |

Observación: relación **1:1** entre observadores y requests. Un solo hecho
físico genera 20 peticiones al LLM. Es exactamente la tormenta que debe
resolver el Social Director (Aclaración 4 y 6).

### R6. Probe existente (`npc_ai_performance_probe`)

| escenario | wall_us | detalle |
|---|---|---|
| A: 10 000 polls sin NPC cercano | 618 | async_main_thread 0,0007 µs/call |
| B: 1 NPC ocioso, 500 evaluaciones | 142 | combat_social ~0 µs |
| C: 4 NPC ociosos, 500 c/u | 656 | combat_social 0,05 µs/call |
| D: 4 seguidores moviéndose, 100 turnos | 6 871 | combat_snapshot 29,5 µs |
| E: 4 NPC + 12 zombis, 200 c/u | 247 | combat_social 0,004 µs/call |
| F: 500 eventos físicos | 1 297 | event_publish 2,19 µs/call |
| G: 20 prompts ordinarios | 119 334 | context 5 964 µs, percepción 5 689 (95,4 %) |
| H: 5 prompts detallados | 539 175 | context 107 832 µs, percepción 107 478 (99,7 %) |

---

## Protocolo de medición corregido

Las cifras de la sección RAW se tomaron sin semilla fija. Se detectó que el
tamaño de prompt depende de la generación aleatoria del NPC: entre dos
ejecuciones sin semilla los bytes variaban hasta 2,4× (11 678 → 27 377 para el
mismo saludo). **Toda medición comparable debe usar `--rng-seed 1`.**

Con semilla fija los bytes son exactamente reproducibles entre ejecuciones
(verificado: 9847 / 9765 / 9767 / 16475 / 16491 / 9865 idénticos en dos corridas
consecutivas). Los tiempos siguen variando ±5-10 % por ruido de máquina.

Además, el gate de la Fase 0 permite un A/B mucho más limpio que comparar dos
compilaciones distintas: **mismo binario, misma semilla, logging on/off por
`CDDA_NPC_AI_DEBUG`**. Eso aísla el coste del logging de cualquier otra
diferencia.

---

## BASELINE RAW vs CLEAN — A/B controlado (semilla 1, mismo binario)

RAW-equivalente = `CDDA_NPC_AI_DEBUG=1`. CLEAN = variable ausente.

Salvedad: el RAW-equivalente escribe en el directorio de usuario, mientras que
el RAW real escribía además en una ruta absoluta de OneDrive desde
`npc::pick_up_item()` en cada tick. Ese coste no aparece en este banco porque el
escenario no ejerce pickup dirigido, así que la medición **subestima** el ahorro
real durante un pickup.

### C1. Subsistema de habla espontánea (`npc_ai_spontaneous`, µs/llamada)

| NPC | logging ON | logging OFF | reducción |
|---|---|---|---|
| 1 | 4,56 | 0,76 | 6,0× |
| 5 | 3,66 | 0,43 | 8,5× |
| 10 | 3,68 | 0,41 | 9,1× |
| 20 | 3,13 | 0,57 | 5,5× |

### C2. Hot path completo (µs por turno)

| NPC | logging ON | logging OFF | mejora |
|---|---|---|---|
| 1 | 16 | 11 | 31 % |
| 5 | 102 | 82 | 20 % |
| 10 | 325 | 273 | 16 % |
| 20 | 1 063 | 990 | 7 % |

Lectura: quitar el logging elimina entre 6× y 9× del coste del subsistema
espontáneo, pero sólo un 7-31 % del hot path total, porque el coste dominante es
`build_combat_perception_snapshot` (O(N²)). Ese es el objetivo de la Fase 1.

### C3. BASELINE CLEAN de referencia (semilla 1, logging off)

Es el punto de comparación principal para las optimizaciones arquitectónicas.

| métrica | valor |
|---|---|
| percepción radio 6 / 12 / 20 / 60 (µs) | 2 954 / 5 832 / 14 452 / 114 062 |
| casillas relevantes devueltas (cualquier radio) | 290 |
| candidatos recorridos por llamada | 365 904 |
| prompt saludo | 9 847 B / 13 162 µs |
| prompt "¿cómo estás?" | 9 765 B / 6 021 µs |
| prompt "¿qué ves?" | 16 475 B / 111 355 µs |
| prompt grupal 20 NPC | 680 551 B / 247 416 µs |
| hot path 1 / 5 / 10 / 20 NPC (µs/turno) | 12 / 82 / 294 / 990 |
| combat_snapshot 1 / 5 / 10 / 20 NPC (µs) | 6,35 / 35,35 / 80,09 / 176,60 |
| requests LLM por evento con 20 observadores | 20 |

### C4. Verificación del gate

Con `CDDA_NPC_AI_DEBUG` ausente, los archivos de diagnóstico bajo
`test_user_dir/` quedan idénticos byte a byte y con el mismo timestamp tras una
corrida completa del banco. Con la variable a `1` se actualizan. Cero escrituras
de diagnóstico en juego normal.

Regresión: `[npc_ai]` pasa completo, 119 casos / 1417 aserciones.

---

## FASE 0 — resultados (semilla 1)

### F0.1 Logging de diagnóstico con gate

Ver tablas C1 y C2. Subsistema espontáneo 6-9× más barato; hot path total
7-31 % mejor. Cero escrituras en juego normal.

### F0.2 Reset de estado de sesión

11 mapas estáticos por NPC; `begin_ai_session()` sólo limpiaba 2. Ahora se
limpian todos desde `reset_all_ai_session_state()`.

| módulo | estado | antes |
|---|---|---|
| combat_social | `combat_states` | ya se limpiaba |
| memory | `recent_speech_by_npc` | ya se limpiaba |
| spontaneous | `spontaneous_states` + `global_last_spoken_turn` | **sobrevivía** |
| goal | `goals` + `next_goal_id` | **sobrevivía** |
| survival | `next_warmth_attempt` | **sobrevivía** |
| fire | `start_fire_tasks` | **sobrevivía** |
| vehicle_unload | `unload_tasks` | **sobrevivía** |
| batch_pickup | `food_batches` | **sobrevivía** |
| coordination | `assignments` | **sobrevivía** |
| equipment_memory | `memories` + `loaded_npcs` | **sobrevivía** |
| watchlist | `watch_cache` | **sobrevivía** |

Impacto: los id de personaje se reutilizan entre partidas, así que una entrada
superviviente asignaba silenciosamente los cooldowns, objetivos o tareas de un
personaje a otro distinto en el mundo siguiente, y los mapas crecían durante
toda la vida del proceso.

### F0.3 Enrutado de consultas

`detailed_scene` no cambia sólo el formato: pone el radio de escaneo en
ilimitado (`build_perception_context`, radio 12 → −1). El literal `"que ves"`
estaba en `detailed_scene_query`, así que la pregunta más breve y frecuente
activaba el barrido ilimitado.

| consulta | antes | después | bytes antes | bytes después |
|---|---|---|---|---|
| "Que ves?" | 111 355 µs | **5 491 µs** (20×) | 16 475 | 10 272 |
| "Describe todo lo que ves" | 116 067 µs | 110 208 µs | 16 491 | 17 311 |
| "Vamonos ahora." | percepción activada | **sin percepción** | — | 10 366 |

Además `"ahora"` y `"actualmente"` ya no disparan percepción por sí solos: sólo
cuentan junto a un verbo de percepción. Antes cualquier frase que mencionara el
presente pagaba un escaneo completo.

### F0.4 Coste fijo residual detectado

Tras el arreglo, toda consulta sin percepción sigue costando 6-8 ms y ~10,3 KB
de prompt, incluido un simple "Hola." Es el objetivo de las Fases 2 y 3
(Aclaración 7): reducir datos irrelevantes en lugar de añadir más.

Regresión Fase 0: `[npc_ai]` 121 casos / 1436 aserciones, todo pasa.

---

## FASE 1 — percepción (en curso, semilla 1)

### F1.1 Enumeración por radio

Antes: `points_on_zlevel(z)` para 21 niveles = 365 904 casillas enumeradas y
descartadas después por distancia, así que un radio corto pagaba igualmente el
recorrido completo del mapa. Ahora: `points_in_radius( origin, radio,
fov_3d_z_range )`.

| radio | CLEAN | tras F1.1 | mejora |
|---|---|---|---|
| 6 | 2 954 µs | 1 323 µs | 2,2× |
| 12 | 5 832 µs | 4 809 µs | 1,2× |
| 20 | 14 452 µs | 13 040 µs | 1,1× |
| 60 | 114 062 µs | 107 191 µs | 1,06× |

Elimina un coste fijo de ~1 400 µs por llamada. El beneficio relativo es grande
con radio corto, que es el caso normal.

### F1.2 Corto-circuito de notabilidad — PROBADO Y REVERTIDO

Hipótesis: evitar construir la observación completa (nombres traducidos de
terreno, mobiliario e items) para casillas que luego se descartan.

Medición: 1 362 / 4 994 / 11 953 / 111 449 µs frente a 1 323 / 4 809 / 13 040 /
107 191 sin el cambio. **Diferencia dentro del ruido, en parte peor.**

Conclusión: el coste por casilla no está en construir nombres sino en el
chequeo de línea de visión `who.sees()`, que se ejecuta antes. Además, en un
mapa realista más casillas son notables, así que el pre-test sería un coste
neto. Se revirtió en lugar de dejar complejidad no justificada por datos.

### F1.3 Tope de radio en la escena detallada

El renderizador nunca emite más de `detailed_ordinary_tile_render_limit` = 96
casillas ordinarias, pero el escaneo llegaba a distancia máxima de visión
(307 461 casillas, una prueba de línea de visión cada una). Radio detallado
fijado en 20. Las criaturas se siguen recogiendo a alcance de visión completo
por separado, así que las amenazas lejanas no se pierden.

| consulta | CLEAN | tras F1.3 | mejora |
|---|---|---|---|
| "Describe todo lo que ves" | 116 067 µs | **12 196 µs** | 9,5× |

Evidencia de que no se pierde información: los bytes del prompt son
**idénticos** antes y después (16 491). Las 287 000 casillas adicionales no
aportaban nada al texto generado.

### F1.4 Estado acumulado Fase 0 + Fase 1

| métrica | RAW | CLEAN | actual | mejora vs CLEAN |
|---|---|---|---|---|
| "Que ves?" | 111 355 µs | 111 355 µs | **4 446 µs** | 25× |
| "Describe todo lo que ves" | 116 067 µs | 116 067 µs | **12 196 µs** | 9,5× |
| "Como estas?" | 6 021 µs | 6 021 µs | 4 479 µs | 1,34× |
| percepción radio 6 | 2 954 µs | 2 954 µs | 1 351 µs | 2,2× |
| prompt grupal 20 NPC | 247 416 µs | 247 416 µs | 209 381 µs | 1,18× |

Regresión: `[npc_ai]` 121 casos / 1436 aserciones, todo pasa tras cada cambio.

### F1.5 Snapshot de combate acotado y reuso ocioso

Implementación final:

- `build_combat_perception_snapshot()` reutiliza las listas de percepción que
  vanilla ya calculó en `npc::regen_ai_cache()` en vez de volver a recorrer
  `game::all_creatures()` para cada observador.
- El prompt conserva un máximo de 12 criaturas y cada snapshot completo queda
  limitado a 24 comprobaciones LOS por observador. Jugador, objetivo y hostiles
  tienen prioridad; cada candidato conserva validación LOS propia.
- Los polls ociosos periódicos siguen existiendo, pero reutilizan la lista de
  criaturas ya validada. Una huella compartida por turno de HP, sangrado,
  agarre y retirada fuerza reconstrucción completa cuando cambia estado físico.
- La huella se limpia con `reset_all_combat_social_states()`, por lo que no
  sobrevive a cambios de sesión.

Primera iteración (solo presupuesto de candidatos): 13 / 27 / 33 / 46
µs/NPC/turno y 8,90 / 47,68 / 92,08 / 136,55 µs por snapshot. El trabajo ya
estaba acotado, pero reconstruir 12 aliados sanos cada cinco turnos no cumplía
el gate; no se dio la fase por cerrada.

Resultado final, comando
`npc_ai_baseline_hot_path_scaling --rng-seed 1 -d yes`:

| NPC | CLEAN hot path µs/turno | Fase 1 µs/turno | µs/NPC/turno | combat_snapshot avg_us |
|---|---:|---:|---:|---:|
| 1 | 12 | 16 | 16 | 7,50 |
| 5 | 82 | 83 | 16 | 7,25 |
| 10 | 294 | 231 | 23 | 14,69 |
| 20 | 990 | **456** | **22** | **14,15** |

Gate: 22 / 16 = **1,375×**, menor que 1,5×. Con 20 NPC, hot path total
**990 → 456 µs/turno** (−54 %) y snapshot medio **176,60 → 14,15 µs**
(12,5×). El coste residual principal del benchmark está en `regen_ai_cache()`
vanilla, no en el segundo escaneo de Combat Social.

Validación final con semilla 1:

- `[npc_ai_combat_social]`: 21 casos / 356 aserciones, todo pasa.
- `[npc_ai]`: 122 casos / 1517 aserciones, todo pasa.
- Nuevo test obligatorio #4:
  `combat_social_snapshot_work_is_bounded_per_observer_at_20_npcs`.

**Fase 1 cerrada.**

### F2.1 Self State directo y relevante

Implementación:

- `build_self_snapshot()` separa `physical_state` de `full_inventory`; una
  pregunta corporal no recorre inventario ni llama al snapshot sensorial.
- HP global/por parte, heridas, sangrado, dolor, necesidades, temperatura,
  efectos visibles, moral y stamina se leen de las APIs vanilla.
- Solo se renderizan partes dañadas o afectadas. Se eliminó el bloque duplicado
  `NECESIDADES FISICAS` de `build_npc_prompt()`.
- Stamina permanece estrictamente de solo lectura.

Resultado del benchmark reproducible, build Release x64, semilla 1, media de
5 repeticiones:

| consulta | Fase 1 avg_us | Fase 2 avg_us | Fase 1 bytes | Fase 2 bytes | cambio |
|---|---:|---:|---:|---:|---|
| `Como estas?` | 4479 | **44** | 9765 | **3800** | 101,8×; −61,1 % bytes |
| `Estas herido?` | — | **50** | 9767 | **3802** | ruta corporal directa |

Control de los demás tipos en la misma corrida:

| tipo | avg_us | bytes |
|---|---:|---:|
| greeting | 13 518 | 10 459 |
| scene_short | 5645 | 10 374 |
| scene_detailed | 15 785 | 17 413 |
| memory | 8569 | 10 477 |
| tense_only | 8333 | 10 468 |

Los tipos no propios siguen pagando contexto sensorial y son el objetivo de la
Fase 3. La variación temporal respecto a corridas previas está dentro del ruido
de máquina; los bytes reflejan los campos corporales nuevos y serán recortados
por intención en el router.

Validación final con `--rng-seed 1`:

- `[npc_ai_self]`: 7 casos / 62 aserciones, todo pasa.
- `[npc_ai]`: 125 casos / 1539 aserciones, todo pasa.
- Tests obligatorios #6, #7 y #8 en PASS.

**Fase 2 cerrada.**

### F3.1 Context Router y presupuesto duro

Punto de partida medido justo después de Fase 2, semilla 1:

| NPC | total_us | us/NPC | bytes totales |
|---:|---:|---:|---:|
| 1 | 35 821 | 35 821 | 10 465 |
| 5 | 55 241 | 11 048 | 47 250 |
| 10 | 140 981 | 14 098 | 332 072 |
| 20 | 307 025 | 15 351 | 682 593 |

Implementación:

- Diez intenciones explícitas seleccionan bloques de contexto.
- Self State, inventario, percepción, memoria, vigilancia, espontáneo y
  NPC-a-NPC tienen rutas separadas; saludo/general no escanean escena.
- Percepción sensorial puede renderizarse sin arrastrar inventario/estado propio.
- Presupuestos duros de 8-24 KiB por intención, truncado opcional explícito y
  límite de 2048 bytes para una frase de jugador excepcionalmente grande.
- `Como estan?` conserva un prompt individual por NPC pero no describe al grupo
  entero dentro de cada prompt.

Resultados finales, mismos comandos y semilla:

| consulta | inicio F3 avg_us/bytes | final avg_us/bytes |
|---|---:|---:|
| greeting | 13 518 / 10 459 | **37 / 2963** |
| self_state | 44 / 3800 | 56 / 3800 |
| injury | 50 / 3802 | 98 / 3802 |
| scene_short | 5645 / 10 374 | 6662 / **6201** |
| scene_detailed | 15 785 / 17 413 | 17 372 / **13 240** |
| memory | 8569 / 10 477 | 8296 / **3069** |
| tense_only/general | 8333 / 10 468 | **40 / 2972** |

La subida temporal de Self State/escena está dentro del ruido de máquina; sus
bytes no aumentan y no se añadió trabajo a esas rutas.

| NPC | final total_us | final us/NPC | final bytes |
|---:|---:|---:|---:|
| 1 | 80 | 80 | 3780 |
| 5 | 370 | 74 | 18 911 |
| 10 | 700 | 70 | 37 823 |
| 20 | **2379** | **118** | **75 679** |

Gate grupal: 20 NPC **682 593 → 75 679 bytes** (−88,9 %) y
**307 025 → 2379 µs** (129×). El total queda bajo 160 KiB (20 × presupuesto
de 8 KiB) y cada NPC mantiene su propia respuesta.

Validación final con semilla 1:

- `[npc_ai_context]`: 3 casos / 130 aserciones.
- `[npc_ai_perception],[npc_ai_scene],[npc_ai_self]`: 19 / 167.
- `[npc_ai]`: 128 / 1669.
- Tests obligatorios #5, #9 y #10 en PASS.

**Fase 3 cerrada.**

## FASE 4 — Social Director (semilla 1)

### F4.1 Requests por evento físico compartido

Comando:
`npc_ai_baseline_requests_per_shared_event --rng-seed 1 -d yes`.

| observadores | requests LLM encolados | profundidad de cola | total_us |
|---:|---:|---:|---:|
| 1 | 1 | 1 | 30 010 |
| 5 | 1 | 1 | 21 158 |
| 10 | 1 | 1 | 41 290 |
| 20 | **1** | **1** | **76 499** |

Resultado frente al BASELINE CLEAN a 20 observadores: **20 → 1 request**
(−95 %) y profundidad 20 → 1. Los tiempos se registran como diagnóstico; el
gate estable es el fan-in de requests.

### F4.2 Gates y regresión

- Gate no oculto #11:
  `combat_social_director_limits_a_shared_event_to_one_request_at_20_observers`
  exige exactamente 1 request y profundidad 1.
- Presupuesto social: evento normal 1; evento importante (prioridad ≥97), 2.
- Revalidación focal de supersede urgente, fin de combate, agarres, diálogo
  grupal y órdenes deterministas: **8 casos / 177 aserciones**.
- Build: Release x64 correcto.
- `[npc_ai] --rng-seed 1`: **130 casos / 1723 aserciones, todo pasa**.

**Fase 4 cerrada.**

## FASE 6 — Equipment y órdenes grupales (semilla 1)

### F6.1 Orden grupal determinista por NPC

Comando:
`npc_ai_phase6_group_equipment_order_scaling --rng-seed 1 -d yes`.

| NPC | afectados | fallos | cola LLM | total_us | us/NPC |
|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 0 | 0 | 323 | 323 |
| 5 | 5 | 0 | 0 | 584 | 116 |
| 10 | 10 | 0 | 0 | 1659 | 165 |
| 20 | **20** | **0** | **0** | **3212** | **160** |

La ruta ejecuta una acción física y persiste memoria por NPC, por lo que el
total es O(N). No construye prompts ni encola requests LLM.

### F6.2 Gates y regresión

- Drops involuntarios recuperables: lesión/tumbling, desarme melee y bio-op
  conservan UID, owner, posición real y `retrieval_expected=true`.
- Gates nuevos #19, #20 y #22 más lesión: **4 casos / 82 aserciones**.
- Test #21 de mochila, recuperación del mismo UID y volver a vestir permanece
  verde dentro de `[npc_ai_equipment]`: **26 casos / 327 aserciones**.
- Build Release x64 correcto.
- `[npc_ai] --rng-seed 1`: **134 casos / 1810 aserciones, todo pasa**.

**Fase 6 cerrada.**

## PISTA DE LINTERNA — sidebar y mensajes (semilla 1)

### L1. Indicador cacheado de fuente de luz

Comando:
`[npc_ai_flashlight] --rng-seed 1 -d yes`.

| consultas | detecciones | total_us | ns/consulta |
|---:|---:|---:|---:|
| 100 000 | 100 000 | 11 837 | **118** |

El benchmark usa una `flashlight_on` transportada y precalienta el caché antes
de medir. `display::active_light_indicator()` llama la sobrecarga cacheada
`cache_has_item_with( "item::is_emissive", &item::is_emissive )`; no escanea el
inventario por redibujado. No se usa `LIGHT_300`, porque
`Item_factory::finalize_pre()` convierte `LIGHT_n` en `itype::light_emission` y
elimina el flag.

### L2. Gates y regresión

- Mensajes: el actor on emite `You turn the flashlight on.` y el actor off emite
  `You turn the flashlight off.`; ambos dejan tipo/nombre/estado coherentes.
- Sidebar: `active_light_desc` cambia de `-` a `on` al transportar una fuente y
  vuelve a `-` al retirarla.
- Foco mensajes + widget + benchmark: **3 casos / 66 aserciones**.
- Build Release x64 correcto.
- `[npc_ai] --rng-seed 1`: **136 casos / 1872 aserciones, todo pasa**.
- Test obligatorio #25: PASS.

**Pista de linterna cerrada.**

## DIAGNÓSTICO QWEN / OLLAMA — contrato y latencia (semilla 1)

### Q1. Línea base del contrato antes de corregir B+D

El payload real conserva `model=qwen3:14b`, `stream=false`, `think=false` y
`keep_alive=30m`. No envía `system`, `options`, `temperature`, `top_p`, `top_k`,
`repeat_penalty`, `num_predict` ni stop tokens; todos los parámetros omitidos
dependen del default efectivo de Ollama/Modelfile. Instrucciones, personalidad,
contexto e interacción se concatenan en una única propiedad `prompt`.

El parser de transporte exige JSON válido y extrae solo `response`. La traza
llama RAW al body completo y CLEAN al string `response` desescapado, antes de
los parsers específicos de diálogo espontáneo, Combat Social o selectores.

### Q2. Instrumentación y gate iniciales

Con `CDDA_NPC_AI_DEBUG=1`, `npc_ai_ollama_diagnostics.txt` recibe una muestra
por request y un resumen acumulado por sesión. Se registran promedio/máximo de
bytes de prompt/respuesta, profundidad de cola, preparación, espera, worker,
HTTP, parseo, cola de completion, recogida por el hilo principal, validación y
total. `http_completed_ms` y `parse_completed_ms` ahora proceden de puntos
distintos del cliente; para executors de test sin timestamps se usa el fin de
la ejecución como fallback.

| validación sintética | valor |
|---|---:|
| requests agregadas | 1 |
| éxitos / errores | 1 / 0 |
| bytes de prompt | 23 |
| archivo con gate off | no creado |
| `[npc_ai_ollama] --rng-seed 1` | **2 casos / 32 aserciones** |
| `[npc_ai] --rng-seed 1` | **138 casos / 1904 aserciones** |

No se realizó llamada real a Ollama ni verificación en partida, por alcance
expreso del bloque. Por ello no se publica una cifra artificial de latencia
HTTP; el agregado quedó funcional y comprobado con transporte determinista.

### Q3. Diagnóstico A-F previo a la corrección

**F, combinación**: A (contexto) fue la causa histórica dominante y quedó muy
reducida en Fases 2/3; B (parámetros omitidos/default) y D (instrucciones e
idioma mezclados más validador heurístico) son contribuyentes confirmados; C no
es causa primaria porque el JSON se extrae de forma estricta, aunque los parsers
de ruta son un riesgo secundario; E (modelo) no está aislado ni demostrado.
`qwen3:14b` no se cambió.

### Q4. Corrección B+D: configuración, `system` e idioma

Fecha: 28/08/2026. Build Release x64. Todas las corridas del ejecutable usan
`--rng-seed 1`. El modelo sigue siendo **`qwen3:14b`**.

#### Q4.1 Payload final y justificación

Los parámetros se envían dentro de `options`; `system` es una propiedad de
nivel superior de `/api/generate`.

| parámetro | valor final | justificación para respuestas cortas de NPC |
|---|---:|---|
| `temperature` | `0.4` | reduce deriva, mezcla de idioma y variación no fundada sin convertir toda voz en greedy |
| `top_p` | `0.85` | recorta la cola de baja probabilidad frente al `0.95` del Modelfile local, manteniendo lenguaje natural |
| `top_k` | `20` | conserva un conjunto candidato moderado y fija explícitamente el valor aunque cambie el Modelfile |
| `repeat_penalty` | `1.1` | penalización ligera contra bucles y frases repetidas sin deformar nombres ni selectores cortos |
| `num_predict` | `96` | cubre tres frases cortas y todos los formatos estructurados actuales, limitando divagación y latencia |
| `stop` | `<|im_start|>`, `<|im_end|>` | límites nativos de la plantilla Qwen instalada; evitan fuga de plantilla sin cortar contratos `DECISION/TEXT` multilínea |

Se mantienen `stream=false`, `think=false` y `keep_alive=30m`. El campo
`system` contiene las reglas permanentes, el contrato específico de la ruta y
la personalidad CDDA. El `prompt` contiene solo estado, percepción, memoria,
evento/candidatos y la interacción actual. Los resolutores watch/pickup/wield
también trasladaron sus reglas estáticas a `system`.

#### Q4.2 Bytes antes/después por consulta

Comando:
`npc_ai_baseline_prompt_cost_by_query_type --rng-seed 1 -d yes`.

| consulta | antes prompt/system/total B | después prompt/system/total B | reducción total |
|---|---:|---:|---:|
| saludo | 2963 / 0 / 2963 | **312 / 1315 / 1627** | 45,1 % |
| estado propio | 3800 / 0 / 3800 | **1149 / 1315 / 2464** | 35,2 % |
| herida | 3802 / 0 / 3802 | **1151 / 1315 / 2466** | 35,1 % |
| escena breve | 6201 / 0 / 6201 | **3550 / 1315 / 4865** | 21,5 % |
| escena detallada | 13 240 / 0 / 13 240 | **10 589 / 1315 / 11 904** | 10,1 % |
| memoria | 3069 / 0 / 3069 | **418 / 1315 / 1733** | 43,5 % |
| orden/general | 2972 / 0 / 2972 | **321 / 1315 / 1636** | 45,0 % |

El presupuesto ahora cuenta `prompt + system`, de modo que separar roles no
puede saltarse los límites de Fase 3. En el escalado grupal de estado propio:

| NPC | antes prompt/system/total B | después prompt/system/total B |
|---:|---:|---:|
| 1 | 3780 / 0 / 3780 | 1149 / 1315 / **2464** |
| 5 | 18 911 / 0 / 18 911 | 5654 / 6576 / **12 230** |
| 10 | 37 823 / 0 / 37 823 | 11 305 / 13 158 / **24 463** |
| 20 | 75 679 / 0 / 75 679 | 22 638 / 26 321 / **48 959** |

#### Q4.3 Tasa real de reintento por idioma

Benchmark oculto opt-in:
`npc_ai_ollama_live_language_retry_rate --rng-seed 1 -d yes`. Usa 12 turnos
con UI `es_ES`: seis frases normales y seis frases adversariales que piden
explícitamente responder en inglés. Ejecuta el transporte real, el validador
`generated_text_matches_dialogue_language()` y el único reintento FIFO. No
forma parte de `[npc_ai]` y requiere Ollama local.

| versión | muestras | prompt total/promedio B | system total/promedio B | reintentos | tasa | fallos finales |
|---|---:|---:|---:|---:|---:|---:|
| antes B+D | 12 | 41 175 / 3431 | 0 / 0 | 4 | **33,33 %** | 0 |
| después B+D, corrida primaria | 12 | 9363 / 780 | 17 088 / 1424 | 3 | **25,00 %** | 0 |
| después B+D, repetición | 12 | 9363 / 780 | 17 088 / 1424 | 3 | **25,00 %** | 0 |

Resultado comparable primario: **-8,33 puntos porcentuales** y **-25 % de
reintentos relativos** (4 -> 3), con cero respuestas finales en idioma
incorrecto. Los bytes combinados del corpus bajan de 41 175 a 26 451
(-35,8 %).

Hallazgo de ajuste conservado: poner la corrección de reintento solo en
`system` no bastó frente a una orden contradictoria al final del texto del
jugador (5/12 reintentos y 5 fallos finales en esa corrida intermedia). La
solución final mantiene toda regla estática en `system`, agrega solo el dato
dinámico `OUTPUT_LANGUAGE=<idioma>` al final del prompt y, exclusivamente al
reintentar, repite la corrección dinámica al final de ambos campos. El
validador también rechaza respuestas inglesas cortas como `Okay.` o
`Sure, sounds good.` sin rechazar nombres de entidades sin traducir.

`--rng-seed 1` hace reproducibles NPC, contexto y bytes, pero no fija un seed
de muestreo en Ollama; por eso se registra la repetición posterior y no se
presentan los tiempos HTTP como un benchmark controlado.

#### Q4.4 Gates

- Build Release x64: PASS.
- `[npc_ai_ollama] --rng-seed 1`: **2 casos / 34 aserciones**.
- contexto + idioma: **7 casos / 177 aserciones**.
- `[npc_ai] --rng-seed 1`: **138 casos / 1912 aserciones**.

---

## SESIÓN COMBAT SOCIAL — baseline congelado y capacidad Ollama (29/08/2026)

### S0. Freeze del working tree RAW

El punto RAW de esta sesión es el working tree que existía antes de tocar código
funcional el 29/08/2026. Es la fuente de verdad; no se reconstruye desde HEAD.

- `git status --short`: 38 archivos modificados; ninguno se descartó ni
  reemplazó.
- `git diff --stat`: **38 files changed, 3201 insertions(+), 862 deletions(-)**.
- Binario RAW usado: `Cataclysm-test-vcpkg-static-Release-x64.exe`, 57 333 760 B,
  timestamp 28/08/2026 22:56:18; versión informada
  `cdda-0.I-2026-06-06-1535-27-g1e24bd1e52-dirty`.
- Gate anterior documentado: `[npc_ai]` 138/138 casos, 1912 aserciones; suite
  completa 1276/1288 casos, 12 fallos. La sección 15 conserva evidencia
  suficiente y no fue necesario repetir una suite completa solo para
  reconstruir el baseline.

Lista exacta congelada de los 12 fallos conocidos, a comparar por nombre:

1. `tname_i18n_order`
2. `Glass_portion_breakability`
3. `mission_goal_condition_test`
4. `limit_mod_size_bonus`
5. `monsters_spawn_eggs`
6. `monsters_spawn_egg_itemgroups`
7. `monsters_spawn_babies`
8. `monsters_spawn_baby_groups`
9. `TranslationDocument_loads_valid_MO`
10. `TranslationManager_translates_message`
11. `TranslationManager_translates_message_with_context`
12. `TranslationManager_translates_plural_messages`

### S1. Configuración RAW exacta

Configuración enviada por `src/npc_ai_client.cpp` antes del cambio de esta
sesión:

| campo | RAW |
|---|---|
| endpoint | `http://localhost:11434/api/generate` |
| model | `qwen3:14b` |
| stream / think | `false` / `false` |
| keep_alive | `30m` |
| temperature / top_p / top_k | `0.4` / `0.85` / `20` |
| repeat_penalty / num_predict | `1.1` / `96` |
| stops | `<|im_start|>`, `<|im_end|>` |
| `num_ctx` | **omitido**; `/api/ps` mostraba **32768**, pero la sonda de entrada alcanzó el techo alrededor de 16384 tokens |
| seed LLM | **omitida** |

Identidad obtenida de la API local, no inferida del nombre:

- Ollama **0.33.1**.
- `qwen3:14b`, digest
  `bdbd181c33f2ed1b31c972991882db3cf4d192569092138a7d29e973cd9debe8`.
- Qwen3 **14.8B**, GGUF `Q4_K_M`, 14 768 307 200 parámetros, ventana máxima
  declarada por el modelo **40960**, embedding 5120, 40 bloques.
- Modelfile externo: temperature 0.6, top_p 0.95, top_k 20,
  repeat_penalty 1 y los mismos dos stops. El payload del juego prevalece en
  las opciones que sí declara.

### S2. Definiciones fijadas antes del A/B social

- **Línea útil:** candidata validada y realmente emitida al juego.
- **Evento narrable:** evento que cumple los criterios objetivos de elegibilidad
  social antes del LLM.
- **Evento verbalizado:** `event_id` que respalda al menos una línea finalmente
  emitida.
- Volumen se reporta tanto para el grupo completo por minuto como por
  NPC/speaker por minuto, con distribución/fairness cuando hay varios.
- RAW y NEW usan la misma traza fija, modelo, `num_ctx` efectivo, parámetros de
  inferencia, temperatura y seed LLM cuando el transporte lo permita. Si no es
  determinista se comparan varias muestras mediante mediana y rango.

### S3. Benchmark real de Ollama

El benchmark opt-in existente se ejecutó primero, sin tocar partidas ni DB:

`npc_ai_ollama_live_language_retry_rate --rng-seed 1 -d yes`

Resultado: **12/12** transportes correctos, 3 reintentos de idioma (25 %), cero
fallos finales, 9363 B de prompt y 17076 B de system; 17,477 s para el caso
completo. Esta prueba sigue siendo secuencial y no se usa por sí sola como
medición de cola.

Después se usó un prompt social fijo y el mismo payload RAW. Se descargó el
modelo solo de RAM antes de la primera muestra; no se escribió memoria
persistente ni estado de partida.

| régimen | muestras | concurrencia | media cliente | p95 cliente | observación |
|---|---:|---:|---:|---:|---|
| cold start | 1 | 1 | **4919 ms** | n/a | total servidor 4910,62 ms; load 4260,85 ms |
| warm secuencial | 12 | 1 | **382,42 ms** | **462 ms** | rango 238–462 ms; ~14,58 tokens de salida |
| burst concurrente | 8 | 8 | **1429,88 ms** | **2435 ms** | wall 5344 ms incluyendo arranque de 8 jobs; rango cliente 721–2435 ms |

En el burst las ocho peticiones se lanzaron concurrentemente. Profundidad de
entrada observada: **máxima 8, p95 8** (serie de profundidad al admitir la
ráfaga); las duraciones de servidor crecieron hasta 2151,69 ms mientras cada
evaluación individual permaneció alrededor de 376–413 ms, evidencia de espera
real bajo contención. El runner siguió mostrando `context_length=32768` y
14 545 311 497 B de VRAM.

Conclusión de capacidad para el diseño: warm Ollama admite ráfagas pequeñas,
pero ocho solicitudes independientes multiplican la latencia p95 por ~5,3. El
gameplay debe conservar un worker asíncrono, batching y admisión acotada. El
volumen sostenible no puede provenir de una petición por NPC/evento; si el
batch no basta para el volumen deseado, el Bloque C será necesario.

### S4. Contexto efectivo, truncamiento y configuración explícita

El hallazgo de truncamiento silencioso se comprobó directamente. La columna
RAW de `/api/ps` mostraba 32768, pero una entrada repetitiva de 175 014 B fue
recortada a `prompt_eval_count=16386`; el `system` inicial desapareció y Qwen
respondió con una explicación matemática no relacionada. Una sonda menor de
30 014 B consumió 6035 tokens sin truncarse. Por tanto, la columna del runner
no era suficiente para probar el límite de entrada por petición:
`prompt_eval_count` debe conservarse en cada respuesta.

Medición RAW de prompts reales del juego, con `--rng-seed 1`:

| ruta | prompt + system B | prompt_eval_count |
|---|---:|---:|
| saludo | 1627 | 402 |
| estado propio | 2464 | 682 |
| herida | 2466 | 684 |
| escena breve | 4865 | 1507 |
| escena detallada (peor caso medido) | **11904** | **3497** |
| memoria | 1733 | 429 |
| general | 1636 | 405 |
| grupo de 20, máximo por petición | ~2450 | **689** |

El grupo de 20 no es una petición de 48 959 B: son **20 peticiones
individuales**; 49 005 B y 13 645 tokens son la suma del grupo. Resultado RAW:
**0/27 peticiones truncadas**. El 25 % previo de reintentos de idioma no podía
atribuirse al truncamiento de este corpus concreto.

Configuración NEW aplicada:

- `num_ctx=16384` explícito, lejos del máximo 40960 del modelo y suficiente
  para el peor prompt medido (3497 tokens).
- `num_predict=192` + margen reservado de 512 tokens. El primer A/B batch con
  96 cortó el JSON de cuatro candidatas; 192 permitió 108 tokens de salida y
  cierre sintáctico completo.
- `seed=1` para el A/B reproducible.
- guarda dura conservadora: `prompt.size() + system.size() <= 15680` bytes. El
  Context Router limita sus presupuestos existentes a esa cota; la cola y el
  transporte vuelven a rechazar como defensa final. Un batch futuro debe
  reducir hechos/candidatas/cohorte antes de encolar si no cabe.
- `prompt_eval_count`, `eval_count` y `context_truncated` quedan en cada
  `ai_response` y en el diagnóstico agregado.

Después de fijar la opción, `/api/ps` muestra exactamente **CONTEXT 16384** y
VRAM 11 827 402 505 B, frente a 14 545 311 497 B observados con el default
implícito. El peor prompt repetido consumió 3413 tokens y el máximo de grupo
689; otra vez **0/27 truncados**.

A/B de idioma posterior, dos corridas con seed LLM fija: **4/12 reintentos
(33,33 %) en ambas**, cero fallos finales, máximo 1795 tokens. La fijación de
contexto no redujo por sí sola la tasa; la repetibilidad demuestra que esos
cuatro casos son el efecto del corpus adversarial/modelo, no truncamiento ni
ruido de muestreo.

### S5. Auditoría compacta A y registro emocional D

- El enum real contiene **39** tipos, no 38. Hay 36 productores nativos entre
  hooks y transiciones del snapshot; `ally_dragged`, `ally_critical_hit` y
  `player_critical_hit` son aliases relativos alcanzables. Tipos funcionalmente
  muertos: **0**.
- Hooks directos por búsqueda: `npc_attack` (melee Character, monster y
  projectile), `npc_grabbed`, `weapon_jammed`, `failed_escape`, `grab_broken`,
  `dragged`, `significant_critical`, `attack_missed`, `dodge`, `ally_saved`,
  `heal_started`, `heal_completed`; `enemy_killed` se captura desde muerte real.
- Se añadieron solo metadatos ausentes a hits normales: actor/tirador, target,
  body part, daño, modo melee/ranged y claim máximo. `is_limb_broken()` es la
  única fuente de `LIMB_DISABLED_CONFIRMED`; un hit ordinario queda en
  `HIT_ONLY`. La curación ya tenía healer/patient y ahora conserva parte tratada.
- `importance` no se infló: el crítico sigue en 88 para memoria. Una
  `speak_priority` C++ separada lo eleva a 96 para urgencia/scheduler.
- Snapshot emocional dinámico por petición: pain, morale, panic/fear, stamina,
  grabbed y HP. Reglas permanentes en system; valores actuales en prompt; cero
  inferencias adicionales y cero escrituras a mecánicas vanilla.
- Lifecycle confirmado: ring 256, sequence monotónica, generación de sesión y
  reset desde `begin_ai_session/end_ai_session` a través de
  `reset_all_ai_session_state()`.

### S6. Banco fijo RAW/NEW de Combat Social

Fixture guardado: `tests/data/npc_ai_combat_social_trace.json`. Definiciones
congeladas de S2. Modelo/parámetros idénticos, seed LLM 1; los event IDs de cada
replay son monotónicos de sesión, pero el orden, tipos, actores, targets,
outcomes, tiempos y conjunto de testigos provienen exactamente del mismo
fixture.

Ventana medida: burst de 16 segundos simulados, no tasa sostenida objetivo.

| métrica | RAW | NEW |
|---|---:|---:|
| líneas grupo/min | 18,75 | 15,00 |
| inferencias/min | 18,75 | 3,75 |
| líneas útiles/inferencia | 1,00 | 4,00 |
| eventos narrables capturados | 5 | 5 |
| eventos narrables verbalizados | 4 | 4 |
| cooldown | 16 | 0 |
| deduplicación | 0 | 0 |
| expiración | 0 | 0 |
| conocimiento | 0 | 0 |
| validación | 0 | 0 |
| filtro promesas | 0 | 0 |
| fallback | 0 | 0 |
| cola máxima / p95 | 2 / 2 | 1 / 1 |

Distribución/fairness: RAW = 2/2/1 líneas (7,5/7,5/3,75 por speaker/min);
NEW = 2/1/1 (7,5/3,75/3,75). NEW entrega las cuatro líneas del burst con una
inferencia, una línea de grupo por turno y sin telepatía. El primer intento con
`num_predict=96` produjo 1 línea, 2 inferencias y fallback=1 porque el JSON fue
truncado por límite de salida; se corrigió a 192 y se repitió el A/B completo.

Test end-to-end independiente con combate CDDA real y `--rng-seed 1`:
`npc_ai_real_monster_melee_records_grounded_hit_metadata_end_to_end` PASS; el
ataque real produjo atacante, víctima, parte, daño, modo y claim confirmado.

---

## FINAL — cierre de sesión Combat Social (29/08/2026)

### F1. Resultado funcional

- Cerrados: Paso -1, Paso 0, Paso 0-BIS y bloques **A, D, B y V**.
- Reutilizados, no reconstruidos: Event Stream, lifecycle/reset, Context Router,
  Social Director, persistencia y SQLite existentes.
- Pendientes del plan vigente: **C, E, F y G**. C será necesario si el volumen
  deseado en gameplay real supera el techo conservador medido de Ollama; E debe
  reemplazar más adelante el filtro de promesas por asignaciones respaldadas.
- Cambio jugable: cooldown snapshot normal/urgente 30/5 s → 8/2 s, críticos con
  `speak_priority` separada, lotes de hasta 5 hechos y 4 líneas espaciadas y
  expirables. Toda inferencia está aislada por firma exacta de conocimiento.

### F2. Ollama y truncamiento

Configuración final explícita: `qwen3:14b` Q4_K_M, `num_ctx=16384`,
`num_predict=192`, `seed=1`, temperature 0.4, top_p 0.85, top_k 20,
repeat_penalty 1.1. `/api/ps`: CONTEXT 16384, VRAM 11 827 402 505 B.
Cada respuesta conserva `prompt_eval_count`; alcanzar el contexto se trata como
violación de invariante. Guarda pre-envío conservadora: 15 680 B. Peor prompt
real: 3413 tokens; grupo de 20: 689 tokens por petición; **0/27 truncados**.

Capacidad: cold 4919 ms; warm n=12 media 382,42 ms/p95 462 ms; burst n=8,
concurrencia 8, media 1429,88 ms/p95 2435 ms; profundidad de admisión máxima/p95
8/8. El gameplay permanece asíncrono y una cola saturada reduce volumen, nunca
framerate. Idioma después de fijar contexto: 4/12 reintentos (33,33 %) en dos
corridas deterministas, cero fallos finales y cero truncamientos.

### F3. A/B social autoritativo — misma traza

Ventana de burst de 16 s, cinco hechos en cuatro segundos:

| métrica | RAW | NEW |
|---|---:|---:|
| líneas útiles grupo/min | 18,75 | 15,00 |
| inferencias/min | 18,75 | **3,75** |
| líneas útiles/inferencia | 1,00 | **4,00** |
| eventos narrables capturados/verbalizados | 5/4 | 5/4 |
| cooldown | 16 | **0** |
| dedup / expiry / conocimiento | 0 / 0 / 0 | 0 / 0 / 0 |
| validación / promesas / fallback | 0 / 0 / 0 | 0 / 0 / 0 |
| cola máxima / p95 | 2 / 2 | **1 / 1** |

Fairness: RAW 2/2/1 líneas, 7,5/7,5/3,75 por speaker/min; NEW 2/1/1,
7,5/3,75/3,75. NEW prioriza el burst natural exigido de cuatro líneas y una
sola inferencia; no intenta sostener artificialmente 45-60 líneas/min cuando no
hay hechos. La mejora audible sostenida proviene del gap 8/2 s y de servir
varias candidatas por lote. El filtro de promesas descartó 0 aquí y queda
instrumentado para gameplay real sin cambiar todavía su comportamiento.

### F4. Builds y gates

- Build Release x64 target tests: **PASS**.
- Build Release x64 juego (`cataclysm-tiles.exe`): **PASS**.
- `[npc_ai] --rng-seed 1`: **141/141 casos, 1970 aserciones, PASS**.
- Suite completa `--order decl --rng-seed 1`: **1291 casos; 1279 PASS / 12
  FAIL; 19 aserciones fallidas; 1299,04 s; sin crash**.
- Comparación por nombre: los 12 FAIL son exactamente los congelados en S0;
  ningún caso NPC AI ni nombre nuevo. Una primera pasada 13/22 estaba
  contaminada por un logro `Alpha Avatar` previo; `achievements_tracker` pasó
  aislado 1/59 tras mover solo esos artefactos a un respaldo reversible y la
  repetición completa quedó en los 12 nominales.
- `git diff --check`: PASS; solo avisos esperados LF→CRLF.

No se hizo commit ni push y no se descartó ningún cambio previo del working
tree.

---

## Recogida dirigida: conservación y candidatos (29/08/2026)

| validación | resultado |
|---|---:|
| reproducción antes del fix | 2 casos / 18 aserciones / 2 FAIL |
| regresiones nuevas después del fix | 4 casos / 66 aserciones / PASS |
| pickup + wield + equipment | 37 casos / 475 aserciones / PASS |
| item_location + wield vanilla | 7 casos / 1216 aserciones / PASS |
| gate baseline anterior `[npc_ai]` | 157 casos / 2203 aserciones / PASS |
| gate actual `[npc_ai] --rng-seed 1` | **161 casos / 2269 aserciones / PASS** |
| diferencia del gate | +4 casos / +66 aserciones / 0 regresiones |
| build `cataclysm-tiles.exe` Release x64 | **PASS** |

El resolver compacto mantuvo 35/35 `glass_shard` físicos sin cambios y los
representó una sola vez antes del top-30; `fire_ax` permaneció en candidatos.
Las pruebas de transferencia verifican exactamente una instancia en ground,
storage o wield, daño y variable de identidad conservados, rechazo previo sin
retirar el origen y dos ciclos consecutivos sin target residual.

El diff check de producción/tests/documentación pasa. El diff check global
sigue enumerando únicamente espacios finales preexistentes de los logs de
gameplay locales, que se preservaron deliberadamente como evidencia.

---

## Intencion de adquisicion, ownership, color y parser (29/08/2026)

| validacion | resultado |
|---|---:|
| parser espontaneo `[npc_ai_spontaneous]` | 3 casos / 26 aserciones / PASS |
| pickup/color `[npc_ai_pickup]` | 15 casos / 338 aserciones / PASS |
| intencion `[npc_ai_acquisition]` | 3 casos / 176 aserciones / PASS |
| ownership `[npc_ai_command_ownership]` | 3 casos / 107 aserciones / PASS |
| equipment/conservacion | 17 casos / 200 aserciones / PASS |
| baseline de entrada `[npc_ai] --rng-seed 1` | 161 casos / 2269 aserciones / PASS |
| gate actual `[npc_ai] --rng-seed 1` | **167 casos / 2503 aserciones / PASS** |
| diferencia | +6 casos / +234 aserciones / 0 regresiones |
| build `cataclysm-tiles.exe` Release x64 | **PASS** |

La adquisicion mantiene exactamente una instancia logica del target en un solo
destino valido; los tests cubren storage, wield, worn y ground, incluyendo
estado/identidad real. A11 queda cerrado: el target dirigido no se sustituye
por otra arma mejor valorada. Los campos de intencion son transitorios y no se
serializan. `git diff --check` de produccion/tests/documentacion: PASS (solo
avisos de normalizacion LF->CRLF).
