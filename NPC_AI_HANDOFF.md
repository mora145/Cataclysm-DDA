# QUICK START (leer solo esto)

## Estado actual

- Base real de conversación NPC cerrada, 29/08/2026, sobre checkpoint
  `adf1608531`: el origen de conversación sobrevive en la petición async como
  `DIRECT_PLAYER_DIALOGUE`, `GROUP_PLAYER_DIALOGUE`,
  `SPONTANEOUS_WORLD_EVENT` o `NPC_INITIATED_SOCIAL`. La charla directa solo
  encola/resuelve el target; una réplica NPC->NPC anterior a una nueva pregunta
  directa se descarta al completarse, mientras las cadenas espontáneas y las
  iniciadas por NPC conservan profundidad máxima 1.
- El Context Router reconoce HEALTH, PERCEPTION, INVENTORY_EQUIPMENT y
  CURRENT_SITUATION con variantes ES/EN. Construye el contexto por NPC desde
  fuentes vanilla: HP actual/máximo por parte, dolor, sangrado y efectos;
  visibilidad real; inventario y objeto empuñado reales. El daño corporal añade
  severidad relativa solo en consultas pertinentes. Un test captura el prompt
  exacto entregado al ejecutor y el JSON final de Ollama con `id=arm_r`, HP y
  `severidad_dano=grave`.
- Dedupe: no existía candidata compartida ni cache LLM entre speakers; eran
  generaciones independientes y el historial solo era por NPC. Ahora existe
  además una ventana cruzada acotada de 24 líneas/600 turnos, estricta para
  texto idéntico o casi idéntico, sin bloquear frases diferentes sobre el mismo
  evento. Las rutas nuevas de wield/equipment/pickup/batch/npcmove auditadas
  usan fallback español en runtime para los msgids ausentes de `es_ES`, sin
  regenerar `.po`.
- Gates de esta corrección: build tests Release x64 PASS; focos de conversación
  **27/27 casos, 425 aserciones**, contexto/self/percepción/idioma/social
  **29/29, 401**, Ollama+equipment **15/15, 181**; `[npc_ai] --rng-seed 1`
  **157/157 casos, 2203 aserciones, PASS**. Baseline inmediato:
  **147/147, 2027**; diferencia +10 casos/+176 aserciones y cero regresiones.
  Build juego PASS: `C:\CDDA-AI\Cataclysm-DDA\cataclysm-tiles.exe`, versión
  `cdda-0.I-2026-06-06-1535-28-gadf1608531-dirty`.
- La suite completa no se repitió en este bloque: el protocolo vigente pidió
  tests específicos y gate `[npc_ai]`. Se conserva como referencia nominal la
  última corrida completa de 12 fallos históricos listada en la sección 15; no
  se tocó ninguno de esos subsistemas.
- Bloque corto de correccion de pickup, 29/08/2026: una orden concreta puede
  empunar el objeto si no cabe guardado; general, recover y batch comparten la
  misma validacion fisica, y los rechazos de orden se entregan inmediatamente
  por la voz normal del NPC. Builds Release x64 de tests y juego correctos;
  `[npc_ai] --rng-seed 1` verde (**147 casos / 2027 aserciones**).
- Por prioridad explicita del jugador, la suite completa sin filtro **NO se
  ejecuto para este bloque**: sigue pendiente `--order decl --rng-seed 1` y se
  debe comparar por nombre contra los 12 fallos del baseline congelado cuando
  termine la prueba en partida. No confundir esta pendiente con la corrida
  global contra baseline de la sesion anterior descrita debajo.
- Working tree es la fuente de verdad. El checkpoint inmediato de este cierre
  es `adf1608531`; `1e24bd1e52` queda como checkpoint histórico anterior. El
  log `npc_ai_spontaneous_runtime.txt` sigue fuera de alcance.
- Gate final de la sesión Combat Social, 29/08/2026: builds Release x64 de
  juego y tests correctos; `[npc_ai] --rng-seed 1` verde (**141 casos / 1970
  aserciones**). La evidencia completa está en la sección 15.
- Última suite completa anterior, verificada con `--order decl --rng-seed 1`:
  termina sin abort
  ni acceso nulo; **1291 casos, 1279 pasan y 12 fallan / 19 aserciones
  fallidas**. Son por nombre exactamente los 12 del baseline congelado; no hay
  regresión NPC AI. El detalle y la limpieza reversible de un artefacto previo
  de `test_user_dir` están en la sección 15.
- Fase 0 completa.
- Fase 1 completa: percepcion por radio, limite detallado y hot path de Combat
  Social escalable. Con 20 NPC: 22 us/NPC/turno frente a 16 con uno (1,375x),
  dentro del gate de 1,5x; total 456 us/turno frente a CLEAN 990.
- Fase 2 completa: Self State usa HP/heridas/sangrado/temperatura/efectos/moral
  vanilla; stamina es solo lectura. `Como estas?` no escanea escena ni inventario:
  44 us y 3800 bytes frente a 4479 us y 9765 bytes al cerrar Fase 1.
- Fase 3 completa: Context Router con 10 intenciones y presupuestos duros.
  Con 20 NPC, `Como estan?` baja de 682593 a 75679 bytes y de 307025 a
  2379 us; cada NPC conserva prompt individual con su estado/persona.
- Fase 4 completa: Social Director central con presupuesto 1 normal / 2
  importante; el benchmark compartido encola 1/1/1/1 requests para
  1/5/10/20 observadores. El gate no oculto #11 pasa con 20 observadores.
- Fase 6 completa: los tres drops involuntarios recuperables (lesión, desarme
  melee y bio-op) preservan UID/owner y activan recuperación; órdenes grupales
  resuelven el objeto propio de cada NPC sin LLM. A 20 NPC: 3212 us totales,
  160 us/NPC y cola LLM 0.
- Pista de linterna completa: los mensajes on/off se validan contra sus actores
  reales y el sidebar muestra `Light source: on/-` mediante la búsqueda cacheada
  de `item::is_emissive`. Coste final: 118 ns por consulta cacheada.
- Paso 0 y bloques A+D+B+V completados sin reconstruir el stream. Ollama queda
  fijado a `qwen3:14b`, `num_ctx=16384`, `num_predict=192`, `seed=1`; cada
  respuesta conserva `prompt_eval_count` y cualquier truncamiento se rechaza.
  La guarda pre-envío reserva salida+margen y degrada el lote antes de encolar.
- Combat Social agrupa hasta cinco hechos por firma exacta de conocimiento y
  obtiene hasta cuatro candidatas. C++ fija hablante, hechos permitidos,
  prioridad, expiración y audiencia; el modelo solo aporta texto/claim
  expresivo. Las líneas se reparten a una por turno de grupo y expiran a 12 s.
- Traza fija `five_facts_four_seconds`: NEW = 4 líneas / 1 inferencia, cola
  máxima/p95 1, cero descartes de conocimiento y cero fallback; RAW = 5 líneas
  / 5 inferencias, cola máxima/p95 2. Distribución NEW: 2/1/1 líneas.
- El A/B de idioma final sigue en 4/12 reintentos (33,33 %), determinista y sin
  fallos finales: no era truncamiento en ese corpus. Con 20 NPC, 0/27 prompts
  medidos se truncaron.

### Correccion pickup/wield: detalles y gates

- `npc::ai_request_pickup` conserva `would_take_that`, propiedad,
  `NO_NPC_PICKUP`, visibilidad, ruta, peso y reglas vanilla. Si `can_stash`
  falla, valida el mismo objetivo con el adaptador compartido de
  `npc_ai_wield.cpp` y lo empuna mediante la accion fisica vanilla.
- Si ya hay algo en las manos, primero intenta conservarlo vestido/guardado.
  Si no puede, valida `can_drop`, lo deja fisicamente en el suelo y comprueba
  que el `item_location` objetivo siga valido antes de empunarlo. Batch no
  autoriza esta sustitucion silenciosa; recover y pickup concreto si.
- Los rechazos asincronos se encolan con `npc::say` y se procesan de inmediato
  por el canal normal de sonido. Los fallos grupales tambien tienen un NPC
  hablante y un motivo concreto.
- Recover deja caer a pickup general una orden singular como `recoge tu espada`
  cuando no existe memoria de un arma perdida. Su lista acotada incorpora
  espada, lanza, bate, martillo, palanca, maza y equivalentes ingleses; el LLM
  sigue eligiendo el objeto concreto entre candidatos reales.

Durante el primer gate hubo tres accesos invalidos, todos con el comando
`.\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai]" --rng-seed 1` y el
caso `npc_ai_spanish_pickup_name_selects_the_model_chosen_real_candidate`.
La ruta nueva no se alcanzaba: el fixture conservaba 30 objetos del avatar
anterior porque limpiaba el mapa antes del avatar, la espada quedaba fuera del
limite, el fake devolvia indice 0 y la prueba llamaba igualmente a la ruta
vanilla `who.pick_up_item()` sin pickup dirigido. Se limpio el residuo despues
del avatar y se agrego un `REQUIRE` antes de procesar/ejecutar el pickup.

Auditoria de invalidacion: `item_location` usa referencia segura; nombres y
tipos se copian antes de `wield`/`obtain` y el objetivo no se desreferencia
despues. Como defensa extra, toda suelta previa es explicita y se revalida el
objetivo. El snapshot `C:\CDDA-AI\backups\snapshot_20260829_0212` no contiene
la prueba nueva y su gate congelado era 141/141, 1970 aserciones, sin crash.

Gates de este cierre parcial:

- Build Release x64 tests: **PASS**.
- `[npc_ai_equipment_regression] --rng-seed 1`: **12 casos / 117 aserciones,
  PASS**.
- `[npc_ai] --rng-seed 1`: **147 casos / 2027 aserciones, PASS**, sin crash.
- Build Release x64 juego `cataclysm-tiles.exe`: **PASS**.
- Suite completa sin filtro: **PENDIENTE POR ORDEN EXPLICITA DEL JUGADOR**.

## Siguiente accion concreta

**Prioridad inmediata:** el jugador prueba este `cataclysm-tiles.exe` en partida
real. Verificar charla individual con Kim/Liam, salud real, percepción,
inventario/equipo, charla grupal por estado individual, continuidad de charla
NPC->NPC, ausencia de frases clonadas y mensajes de equipo en español. Revisar
después `npc_ai_ollama_diagnostics.txt` con `CDDA_NPC_AI_DEBUG=1` para comparar
PLAYER_DIALOGUE / SPONTANEOUS / NPC_TO_NPC_REPLY. No iniciar todavía el bloque
grande de eventos de combate; los nuevos logs de gameplay deciden la sesión
siguiente.

1. A+D+B+V están cerrados como cambio jugable. No reconstruir Fase 5 ni Fase 7:
   el Event Stream, lifecycle, Context Router y SQLite existentes son la base.
   Lo siguiente del plan vigente es **C, E, F y G**, salvo nueva indicación.
2. Si se investiga otra respuesta real, activar temporalmente
   `CDDA_NPC_AI_DEBUG=1`, reproducir una sola interacción y revisar
   `npc_ai_ollama_diagnostics.txt`; no cambiar `qwen3:14b` sin un A/B nuevo.
3. Antes de cualquier fase nueva, conservar como gate el build Release x64,
   `[npc_ai] --rng-seed 1` y una corrida completa sin filtro con
   `--order decl --rng-seed 1`. El filtro NPC es el gate rápido; nunca sustituye
   la corrida completa.
4. El plan de fases 0-7 ya NO es el plan del proyecto. Ver
   "OBJETIVO DEL PROYECTO" justo debajo: las Fases 5 y 7 se reconvierten y el
   trabajo restante es hacer que los NPC se sientan vivos.

## OBJETIVO DEL PROYECTO (leer siempre, define las prioridades)

El dueño del proyecto lo describe así: CDDA es para él un generador de
historias. Quiere leer las conversaciones de sus NPC y ver cómo se relacionan.
Habla con ellos como si fueran personas reales. Quiere **compañeros útiles y
compañeros vivos**, no una de las dos cosas.

Requisitos que se derivan de eso:

- **Hablan mucho, sobre todo en combate.** En un mundo apocalíptico la gente no
  pelea en silencio: grita, insulta, bromea, cita películas, llora de miedo, se
  desespera. Nada de censura artificial que haga que todos suenen igual.
- **Solo dicen cosas ciertas.** CDDA aporta la realidad, el LLM le pone voz. Si
  un NPC dice que le rompió el brazo a un zombi, el evento de CDDA debe
  confirmarlo. Nunca inventar enemigos, heridas, objetos ni sucesos.
- **Se ayudan.** Ver a un compañero herido y actuar. El comportamiento vanilla
  de curar con vendas ya existe: hay que afinarlo y darle voz.
- **Hablan del presente en combate y de la memoria fuera de él.**
- **Identidad estable.** Personalidad derivada de lo que CDDA ya sabe del NPC.
  Si Liam es prudente, sigue siéndolo dentro de tres semanas.
- **Sin pérdida de rendimiento** con varios NPC. Ya conseguido, no perderlo.
- **Misma infraestructura** para aliados, neutrales y hostiles. Sin sistemas
  paralelos. NO se quiere un "AI World Director" que controle clima ni hordas.

### Criterio de éxito, en este orden

1. **Líneas de diálogo por minuto de combate superiores a la línea de base.**
   La línea de base es el gameplay del build del 27/08/2026, donde Kim y Liam ya
   se avisaban, se insultaban y reportaban heridas. Ese registro es el suelo, no
   la meta. Lo juzga el dueño jugando.
2. **Cero hechos inventados.** Toda frase debe poder rastrearse a un evento real
   de CDDA. Verificable con test.
3. **Sin regresión de rendimiento** respecto a las métricas de la sección 13.

### Hallazgo decisivo: el sustrato de hechos YA EXISTE. No reconstruirlo.

`src/npc_ai_event_stream.h` define **39** tipos de hecho (`npc_hit`,
`ally_bleeding`, `enemy_killed`, `significant_critical`, `heal_started`,
`player_grabbed`, …), ya lleva `importance`, `audible_volume`,
`confirmed_outcome` y ya resuelve testigos en `known_by_npc_ids` por
participación, vista u oído en el momento de registrar. Los hooks ya están
puestos en código vanilla: `melee.cpp`, `ranged.cpp`, `monster.cpp`,
`activity_actor.cpp`, `character_escape.cpp` y `mattack_actors.cpp`.

**La Fase 5 tal como estaba redactada ("suscribirse al event_bus y crear el
sustrato") está en su mayor parte hecha por otra vía, con hooks directos que dan
más contexto que el `event_bus`. Rehacerla sería crear el sistema paralelo que
AGENTS.md prohíbe.**

### Hallazgo decisivo: por qué hablan poco

No es falta de hechos. Es el estrangulamiento en `src/npc_ai_combat_social.cpp`:

- `ordinary_request_gap_turns = 30` (línea 45). Un turno es un segundo, así que
  cada NPC habla como mucho **una vez cada 30 s**. Un combate típico dura menos
  que eso: cada NPC dice una frase y enmudece.
- `urgent_request_gap_turns = 5` solo aplica si `importance >= 94` (línea 746).
- El crítico de melé se registra con **importancia 88** (`melee.cpp:909`), es
  decir **por debajo del umbral de urgencia**. El "le rompí el brazo al cabrón"
  que el dueño quiere oír no llega a saltarse el cooldown.
- Presupuesto del Social Director: 1 reacción normal / 2 importantes por evento.
- `combat_social_text_has_unconfirmed_tactical_promise` borra después las
  frases tácticas supervivientes.

Esos números se calibraron para **reducir peticiones**, que era el objetivo de
las Fases 0-4. Ahora el objetivo es el contrario y hay que recalibrarlos, pero
sin volver a las 20 peticiones por evento: de ahí que la reserva pre-generada
sea la que paga el aumento de volumen.

### Plan de bloques que sustituye a las Fases 5 y 7

**Paso 0 — Medir Ollama.** Latencia media y p95 por petición y profundidad de
cola con llamadas reales. **Todo el diseño posterior depende de ese número**: si
una petición tarda más que un combate, el volumen tiene que venir de la reserva
pre-generada y no de peticiones nuevas.

**Bloque A — COMPLETO: auditar el sustrato, no reconstruirlo.** Cobertura
verificada; los 39 tipos son alcanzables (36 con productor nativo y tres
aliases relativos). Se añadieron solo los metadatos concretos ausentes en los
golpes normales: parte corporal, daño, modo, atacante/tirador y grado de claim.
Se confirmó actor/paciente en curación y lifecycle/reset de sesión. Históricamente:
verificar cobertura de
hechos frente a lo que el dueño quiere oír (parte del cuerpo dañada, quién
disparó, quién curó a quién), calibrar los `importance` para que los hechos
narrables buenos superen el umbral de urgencia, y confirmar lifecycle y reset de
sesión. Solo añadir hooks donde falte un hecho concreto.

**Bloque D — COMPLETO: registro emocional.** El prompt de cada lote lleva los
valores actuales de dolor, moral, miedo/pánico, stamina, grabbed y HP; el system
contiene solo las reglas permanentes de interpretación. Todo es solo lectura y
no añade inferencias. Históricamente: modular el tono con dolor, moral, miedo,
stamina, agarrado y HP, que ya están en el combat snapshot. Coste cero en
llamadas al modelo y alto impacto percibido. Hacer pronto.

**Bloque V — COMPLETO (calibración conservadora): volumen de habla.** La ruta
snapshot baja de 30/5 s a 8/2 s en NEW; la ruta RAW del banco conserva 30/5.
Los críticos mantienen importance 88 pero derivan speak priority 96. Los lotes
producen hasta cuatro líneas espaciadas y expiran a 12 s. Históricamente:
recalibrar `ordinary_request_gap_turns`, el
umbral de urgencia y el presupuesto del Social Director para que un combate
produzca muchas líneas y no una. Es el bloque que el dueño va a notar. Debe
medirse en líneas por minuto de combate, no en peticiones ahorradas. El coste NO
se paga volviendo a una petición por NPC y evento, sino con el Bloque B.

**Regla de veracidad por grado (invariante permanente).** El texto nunca puede
afirmar un resultado físico más fuerte que el hecho disponible. Saber que el
golpe fue al brazo autoriza "le di en el brazo"; solo autoriza "le rompí el
brazo" si CDDA confirma el miembro roto o inutilizado. Aplica a cualquier hecho,
no solo a partes del cuerpo.

**La garantía va ANTES de generar, no después.** C++ no puede juzgar
semánticamente si "le hice mierda el brazo" significa golpe o fractura, y
apoyarse en un analizador semántico inexistente sería falsa seguridad. El orden
correcto es: hecho tipado real → grado de afirmación permitido → contexto que
recibe el modelo → texto. Cuando no exista `confirmed_outcome`, el prompt debe
declarar explícitamente el límite de afirmación permitido. Si la salida
estructurada permite un `claim_type` u `outcome_level` sin coste de otra
inferencia, úsalo como dato validable, pero el texto sigue siendo no
autoritativo. La validación posterior comprueba todo lo mecánicamente
verificable y nada más.

**Bloque C — Reserva pre-generada.** Durante la calma, generar y almacenar
frases con la personalidad de cada NPC, clasificadas por tipo de evento y
registro emocional. En combate se disparan al instante, sin esperar al modelo.
Resuelve el problema de que el grito llegue tarde.

**Bloque B — COMPLETO: batch de hechos a varias candidatas.** Lotes de hasta
cinco hechos, hasta cuatro slots C++ y como máximo dos cohortes/inferencias por
sufijo. Cohorte = firma ordenada de event IDs conocidos por cada hablante; un
prompt nunca contiene un hecho que no conozcan todos sus speakers. C++ conserva
autoridad sobre speaker, event IDs, priority, expiry y audience; valida la
respuesta estructurada y permite un único fallback reducido con cola <3.
Históricamente: es la pieza que desacopla
"cuánto pueden decir" de "cuántas inferencias se hacen", y por tanto la que
paga el Bloque V. Una sola petición recibe un pequeño lote de hechos recientes y
devuelve varias `utterance candidates` que solo contienen slot, subconjunto de
event IDs, claim expresivo y texto. El slot remite a metadatos fijados por C++;
el modelo no propone hablante, prioridad, expiración ni audiencia. El Social
Director decide cuáles salen y cuándo, repartidas en los segundos siguientes.

El flujo correcto NO es `evento → Qwen → frase`, que escala mal, ni
`cada 30 s → Qwen → frase`, que es el silencio actual, sino:

`hechos reales → Event Stream → selección y batching → una petición →
varias candidatas → Social Director → diálogo repartido`

**Bloque E — Promesas con respaldo.** Sustituir
`combat_social_text_has_unconfirmed_tactical_promise`
(`src/npc_ai_combat_social.cpp:1075`) por una validación: si la promesa se puede
respaldar con una asignación real vía `delegate_to_helper` / `assignment_for`,
pasa y se ejecuta; si no, se filtra. Hoy ese filtro borra literalmente las
mejores frases del dueño, y de forma arbitraria: "voy por el" se filtra pero
"voy a por ese" se cuela. Aquí hablar y ser útil pasan a ser lo mismo.

**Bloque F — Memoria episódica** (absorbe la Fase 7). Recuerdos con qué pasó,
quién estaba presente, cuándo, procedencia (lo vio / se lo contaron / lo sabe) e
importancia, con recuperación por relevancia en lugar de volcar historial. El
esquema SQLite y los pragmas (WAL, synchronous, conexión persistente, índices)
se diseñan **junto con** este modelo, nunca antes, para migrar una sola vez.
La Fase 7 como "rendimiento de base de datos aislado" queda cancelada.

**Bloque G — Neutrales y hostiles.** Refactor de las puertas `is_player_ally()`
hacia un predicado por actitud. Al final, cuando lo demás esté estable.

### Orden

Paso 0 → A → D → B → V → C → E → F → G → tests restantes → informe final.

B pasa por delante de V porque es quien paga el volumen. C (reserva
pre-generada) baja de prioridad: sus frases son genéricas y no pueden citar lo
que acaba de ocurrir, así que solo sirve para reacciones instantáneas si la
latencia medida en el Paso 0 lo exige.

Ninguna sesión debería cerrarse dejando solo infraestructura sin efecto audible
en partida.

### Invariantes que no se negocian al subir el volumen

- Ninguna petición puede bloquear el gameplay. Todo asíncrono. Una cola saturada
  degrada volumen social, nunca velocidad del juego.
- Toda línea pendiente tiene expiración: un hecho cierto pero obsoleto no se
  pronuncia veinte segundos después.
- Se conservan deduplicación (tres testigos del mismo golpe no dicen lo mismo),
  fairness entre NPC y la posibilidad de interrumpir ante un evento urgente.
- Un NPC solo habla de lo que percibió.
- `importance` tiene semántica para memoria y priorización. No se infla para
  atravesar cooldowns; si hace falta, se deriva una `speak_priority` aparte.

### Aislamiento de conocimiento (la regla que el batching podría romper)

Al meter varios hechos y varios hablantes en una sola petición aparece el
riesgo de que el modelo atribuya a un NPC algo que no presenció. Por eso:

- `known_by_npc_ids` es la autoridad. C++ decide qué NPC puede conocer cada
  hecho **antes** de generar, nunca el modelo.
- El orden es: hechos reales → elegibilidad de hablantes → subconjunto de hechos
  conocido por cada hablante → generación → validación.
- **Etiquetar particiones dentro de un mismo prompt NO es aislamiento.** El
  modelo lee todo lo que hay en el contexto. Si el prompt dice "LIAM SABE: han
  mordido a Sarah / JOHN SABE: se acerca un zombi", nada impide que John diga
  "Sarah, cuidado con esa mordida", y validar eso semánticamente después es
  inviable. La única garantía real es que el modelo **nunca reciba** información
  que el hablante no puede poseer.
- Por defecto, **una inferencia solo puede contener hechos conocidos por TODOS
  los hablantes para los que va a generar texto.** Si dos hablantes tienen
  conocimiento incompatible, no van en la misma inferencia. Formas válidas:
  varios hablantes que comparten todos los hechos del lote; varias candidatas
  para un solo hablante; o agrupar hablantes en cohortes de conocimiento
  equivalente.
- Cómo se calcula la cohorte, con cuidado: **no** se agrupa por el conjunto de
  testigos de cada evento, sino por el conjunto de hechos que cada NPC puede
  usar dentro del lote. Para cada hablante candidato se construye el conjunto
  ordenado de `event_ids` que conoce dentro del lote, se hashea, y se agrupan
  los hablantes con firma idéntica. Cada inferencia recibe exclusivamente los
  hechos de esa firma. También vale usar intersecciones de `known_by_npc_ids` si
  encaja mejor con las estructuras existentes. La propiedad que debe cumplirse
  es que **todo hecho visible para el modelo en una inferencia sea utilizable
  por todos los hablantes de esa inferencia**.
- En la práctica esto no fragmenta mucho: en combate la mayoría de los hechos
  los perciben todos los que están en línea de visión, así que las cohortes
  suelen ser pocas y grandes.

### Autoridad sobre los metadatos: el modelo escribe, C++ gobierna

La salida estructurada no convierte al LLM en autoridad del scheduler. Siempre
que pueda derivarse del juego: el **hablante**, los **event_ids** permitidos, la
**prioridad**, la **expiración** y la **audiencia** los determina C++. El modelo
produce el **texto** y, como mucho, atributos expresivos no autoritativos como
tono o intención. Nunca confiar en una prioridad o una expiración inventadas por
el modelo.
- El hablante lo fija C++, con `speaker_id` preseleccionado o una lista explícita
  de elegibles. Una candidata con hablante no permitido se descarta.
- Validación en C++ tras generar: hablante válido, `event_ids` existentes, el
  hablante conoce **todos** los hechos usados, hechos no expirados, resultado
  confirmado si la frase afirma un resultado, filtros sociales y deduplicación.
  Una candidata que falle se descarta. **Nunca inventar datos para arreglarla.**
- **La regla epistemológica pesa más que el volumen.** Es preferible perder una
  línea que dejar que un NPC hable de algo que no vio, oyó ni protagonizó.
- Degradación acotada si la validación descarta un lote entero: primero
  degradar **dentro del mismo presupuesto** (lote más pequeño, separar cohortes,
  menos candidatas); solo después permitir vuelta a la ruta por NPC, y esa
  vuelta conserva obligatoriamente los límites de concurrencia y el presupuesto
  global. **Nunca una tormenta de peticiones individuales como mecanismo de
  recuperación.** Instrumentar cuántas veces ocurre el fallback. El sistema no
  debe quedar más callado que hoy, pero tampoco puede pagarlo con una avalancha.

### Nota sobre "Fases 5 y 7 canceladas"

Canceladas **como reconstrucciones propuestas**, no como objetivos. Sus metas
funcionales (hechos con testigos, persistencia, memoria) siguen vigentes y se
cumplen reutilizando `npc_ai_event_stream`, la persistencia y el SQLite que ya
existen. No construir sistemas paralelos y no borrar infraestructura existente.

### Contexto histórico

El dueño construyó el sistema de diálogo original y funcionaba; su problema era
latencia por ser síncrono, que ya está resuelto con el worker asíncrono. El
trabajo de las Fases 0-4 y 6 no reemplazó nada suyo: eliminó lo que lo hacía
lento e incoherente (20 peticiones por evento, prompts de 9,8 KB, cero
parámetros enviados a Ollama). Ese margen es lo que ahora permite que hablen
más.

## Build y tests (PowerShell)

```powershell
cd C:\CDDA-AI\Cataclysm-DDA\msvc-full-features
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "Cataclysm-vcpkg-static.sln" /t:"Cataclysm-test-vcpkg-static" /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
cd C:\CDDA-AI\Cataclysm-DDA
.\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai]" --rng-seed 1
.\Cataclysm-test-vcpkg-static-Release-x64.exe --order decl --rng-seed 1
.\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai_combat_social]" --rng-seed 1
.\Cataclysm-test-vcpkg-static-Release-x64.exe "npc_ai_baseline_requests_per_shared_event" --rng-seed 1 -d yes
.\Cataclysm-test-vcpkg-static-Release-x64.exe "npc_ai_phase6_group_equipment_order_scaling" --rng-seed 1 -d yes
.\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai_flashlight]" --rng-seed 1 -d yes
.\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai_ollama]" --rng-seed 1
.\Cataclysm-test-vcpkg-static-Release-x64.exe "npc_ai_baseline_prompt_cost_by_query_type" --rng-seed 1 -d yes
# Opt-in: hace llamadas reales y requiere Ollama local con qwen3:14b.
.\Cataclysm-test-vcpkg-static-Release-x64.exe "npc_ai_ollama_live_language_retry_rate" --rng-seed 1 -d yes
```

Toda medicion comparable usa obligatoriamente `--rng-seed 1`.

## Prohibiciones Git

- No ejecutar `git reset`, `git checkout`, `git restore`, `git clean` ni
  `git revert` destructivo.
- No reemplazar archivos completos por vanilla ni descartar cambios existentes.
- No ejecutar `git commit` ni `git push`.
- No tocar `npc_ai_spontaneous_runtime.txt`; era un log modificado previamente.
- Antes de editar, usar solo inspeccion segura (`git status`, `git diff`).

En sesiones futuras leer primero solo este QUICK START y despues las secciones
puntuales de la fase activa; no releer el documento entero salvo necesidad real.

---

# NPC_AI_HANDOFF.md

Traspaso técnico operativo del trabajo de corrección de la arquitectura NPC AI.

- **Repositorio:** `C:\CDDA-AI\Cataclysm-DDA`
- **HEAD/checkpoint:** `1e24bd1e52` (versión compilada: `cdda-0.I-2026-06-06-1535-27-g1e24bd1e52-dirty`)
- **Rama base real:** `origin/0.I-branch` (tag `0.I`), **no** `origin/master`
- **Agente anterior:** Claude Opus 5 (Cursor). **Agente siguiente:** GPT-5.6 Sol, reasoning MAX, Codex.
- **Fecha del traspaso:** 2026-08-28
- **Estado de compilación:** builds Release x64 de juego y tests correctos
- **Estado de `[npc_ai]`:** 141 casos / 1970 aserciones, todo pasa
- **Estado de suite completa:** finaliza sin crash; 1279/1291 casos pasan, 12
  fallos ajenos a NPC AI idénticos al baseline, inventariados en la sección 15

> **EL WORKING TREE ES LA FUENTE DE VERDAD. HEAD NO REPRESENTA EL ESTADO REAL DEL PROYECTO.**
> Los cambios posteriores de Fase 6 y linterna siguen en el working tree. Ver sección 17.

### Documentos hermanos

- `npc_ai_metrics.md` (raíz) — todas las mediciones RAW / CLEAN / por fase. **Léelo, no repitas las mediciones base.**
- `AGENTS.md` (raíz) — reglas permanentes del proyecto. Siguen vigentes.
- Transcripción completa de la sesión anterior (auditoría + implementación):
  `<RUTA_PRIVADA_LOCAL>\agent-transcripts\session.jsonl` (no forma parte de la publicacion).

---

## 1. OBJETIVO ORIGINAL COMPLETO

Este repositorio es un CDDA modificado con un sistema propio de NPC AI conectado a un LLM local (Ollama). Se hizo una auditoría técnica profunda de ese sistema. **La auditoría NO es el entregable.** El entregable es la arquitectura corregida y terminada.

Objetivos, todos vigentes:

1. **Corregir los problemas encontrados**, no solo describirlos. Críticos, altos, medios técnicamente relevantes y bajos seguros.
2. **Terminar la arquitectura NPC AI**, no parchearla.
3. **Mantener el rendimiento con más NPC.** El sistema debe seguir siendo usable con 20 NPC activos, no solo con 1-4.
4. **Conversación, comportamiento social y coherencia de NPC son prioridad.** No se optimiza a costa de que los NPC se queden mudos o hablen sin sentido.
5. **Preservar toda funcionalidad existente que no sea defectuosa.** Evolución incremental, nunca reemplazo. Ver sección 16.
6. Preparar la arquitectura para NPC **neutrales y hostiles** sin crear un segundo sistema paralelo.

La auditoría se usa como herramienta de diagnóstico, no como checklist ciego: si un hallazgo resulta técnicamente inválido al inspeccionar el código, se descarta y se documenta (ver sección 22).

### Entregables finales A-S

> **AVISO DE FIDELIDAD:** la lista canónica y literal de los entregables A-S está en el "PROMPT MAESTRO" del usuario, en la transcripción enlazada arriba. El agente anterior no conserva su texto exacto. Lo siguiente es una **reconstrucción** a partir de los requisitos que sí se conservan; **Codex debe pedir al usuario la lista literal o recuperarla de la transcripción antes de redactar el informe final.**

Reconstrucción del contenido exigido en el informe final:

- Cambios realizados, archivo por archivo, con justificación.
- Problemas corregidos, con evidencia antes/después.
- Problemas **no** corregidos, con el motivo explícito.
- Bugs adicionales descubiertos durante la implementación.
- Arquitectura final resultante.
- Flujos actualizados (percepción → contexto → decisión → acción → habla).
- Métricas comparadas contra BASELINE RAW y BASELINE CLEAN.
- Estado de los 25 tests obligatorios.
- Diagnóstico de calidad de Qwen/Ollama (sección 19).
- Estado de la preparación aliados/neutrales/hostiles.

---

## 2. ESTADO GENERAL

**Completado aproximado: 65 %** del trabajo total definido.

### HECHO Y VERIFICADO

| Ítem | Estado |
|---|---|
| Reconstrucción del estado Git y de la rama base | Completo |
| Auditoría técnica profunda (6 áreas, subagentes + verificación manual) | Completo |
| Banco de medición reproducible (`tests/npc_ai_baseline_bench.cpp`) | Completo |
| BASELINE RAW | Capturado |
| BASELINE CLEAN | Capturado |
| Protocolo de medición con semilla fija | Establecido |
| Gate de suite completa sin filtro | 29/08/2026: 1291 casos, 1279 PASS / los mismos 12 fallos globales del baseline; 19 aserciones fallidas, sin abort |
| **Fase 0 — higiene** | **Completa** |
| ├─ Gate de logging de diagnóstico | Completo + test |
| ├─ Rutas absolutas de OneDrive eliminadas | Completo |
| ├─ Reset de sesión de los 11 mapas estáticos | Completo + test |
| └─ Matcher de consultas (`que ves` / `ahora`) | Completo |
| **Fase 1 — percepción y hot path de combate** | **Completa** |
| ├─ Enumeración por radio | Completo |
| ├─ Tope de radio en escena detallada | Completo |
| ├─ Corto-circuito de notabilidad | Probado y **revertido** (sección 22) |
| ├─ Snapshot de combate con presupuesto LOS fijo | Completo + test |
| └─ Reuso ocioso invalidado por huella grupal | Completo + benchmark |
| **Fase 2 — Self State desde fuentes vanilla** | **Completa** |
| ├─ HP, heridas, sangrado, temperatura, efectos y moral vanilla | Completo + tests |
| ├─ Consulta corporal sin escena ni inventario irrelevante | Completo + benchmark |
| └─ Stamina de solo lectura y necesidades sin duplicar | Completo + test |
| **Fase 3 — Context Router y presupuesto de prompt** | **Completa** |
| ├─ Clasificación explícita de 10 intenciones | Completo + test |
| ├─ Contexto sensorial separado del Self State | Completo + regresión |
| └─ Presupuesto duro y escalado grupal lineal | Completo + test + benchmark |
| **Fase 4 — Social Director y colas** | **Completa** |
| ├─ Presupuesto social compartido normal/importante | Completo + tests |
| ├─ Fan-in por identidad física sin afectar diálogo directo | Completo + benchmark |
| └─ Gate no oculto con 20 observadores | Completo + test #11 |
| **Fase 6 — Equipment y órdenes grupales** | **Completa** |
| ├─ Memoria en los tres drops involuntarios recuperables | Completo + tests |
| ├─ Mochila: drop, recuperación y volver a vestir | Completo + regresión |
| └─ Órdenes grupales por propietario, sin LLM | Completo + test + benchmark |
| **Pista paralela — linterna** | **Completa** |
| ├─ Mensajes distintos para transición on/off | Verificado + regresión |
| └─ Indicador cacheado en sidebar | Completo + test + benchmark |
| **Pista paralela — diagnóstico y corrección Qwen/Ollama B+D** | **Completa** |
| ├─ Contrato real, formato de prompt y parser documentados | Completo + tests |
| ├─ Trazas raw/limpia/parámetros bajo gate explícito | Completo + test |
| ├─ Latencia agregada por sesión bajo el mismo gate | Completo + test |
| ├─ Parámetros/longitud/stops explícitos; modelo sin cambios | Completo + contrato exacto |
| ├─ Reglas estáticas y personalidad separadas en `system` | Completo + tests + benchmark |
| └─ Idioma reforzado y tasa real final | 4/12 reintentos (33,33 %), determinista; 0 fallos finales; no causado por truncamiento |
| **Paso 0 — Ollama/contexto efectivo** | **Completo**: identidad RAW, cold/warm/concurrente, `num_ctx=16384`, `seed=1`, `num_predict=192`, guarda y telemetría de truncamiento |
| **Bloque A — sustrato existente** | **Completo**: 39 tipos auditados; normal melee/ranged registra actor, parte, daño, modo y claim; curación/lifecycle confirmados |
| **Bloque D — registro emocional** | **Completo**: pain/morale/fear/stamina/grabbed/HP dinámicos, reglas permanentes en system, solo lectura |
| **Bloque B — batch/cohortes** | **Completo**: 5 hechos → hasta 4 slots; aislamiento pre-LLM, metadatos C++, validación y fallback acotado |
| **Bloque V — volumen** | **Completo conservador**: 8/2 s, speak priority separada, líneas espaciadas/expirables y métricas por speaker |

### PENDIENTE

| Ítem | Estado |
|---|---|
| Fase 5 como reconstrucción event_bus | **Cancelada**; objetivos cubiertos reutilizando Event Stream/hook/lifecycle existente |
| Fase 7 como reconstrucción SQLite | **Cancelada**; Bloque F reutilizará persistencia/SQLite existente |
| Bloques C, E, F y G del plan vigente | Pendientes |
| Preparación aliados/neutrales/hostiles | Sin comenzar |
| Muestra de latencia con Ollama real en partida | Opcional; no requerida para cerrar sección 19 |
| Tests obligatorios | 20 de 25; #11, #19-#22 y #25 verificados |
| Informe final A-S | Sin comenzar |

---

## 3. DECISIONES DE ARQUITECTURA

### D1 — Distinción entre persistencia real y logging de diagnóstico

**PROBLEMA.** Se encontraron 12 sitios que abren `std::ofstream` en módulos `npc_ai_*`. A primera vista todos parecían logging de debug candidato a eliminación.

**EVIDENCIA.** Al inspeccionar cada sitio, tres resultaron ser **persistencia funcional**, no diagnóstico:
- `src/npc_ai_watchlist.cpp` ~166 — `save_targets()`, guarda la watchlist del NPC.
- `src/npc_ai_memory.cpp` ~597 — memoria conversacional legacy en archivo.
- `src/npc_ai_world_memory.cpp` ~222 — memoria de mundo del NPC.

**DECISIÓN.** No tocar esos tres. Aplicar el gate solo a los writers de diagnóstico.

**IMPLEMENTACIÓN.** Ver D2.

**POR QUÉ.** Silenciarlos habría borrado funcionalidad de memoria de NPC, que es prioridad del proyecto.

**ARCHIVOS AFECTADOS.** Ninguno de los tres fue modificado.

**RIESGOS.** Que un futuro agente los confunda otra vez con debug. Por eso están listados aquí explícitamente.

**VALIDACIÓN.** Inspección directa de cada sitio de llamada.

---

### D2 — Gate único para todo el logging de diagnóstico

**PROBLEMA.** Los writers de diagnóstico escribían **incondicionalmente**. El caso peor: `npc::pick_up_item()` abría un archivo en una ruta absoluta de OneDrive **en cada tick** mientras hubiera un pickup dirigido activo. En otra máquina esa ruta no existe y el `ofstream` fallaba en silencio, pero el intento de apertura se pagaba igual.

**EVIDENCIA.**
- `src/npcmove.cpp` 3767-3770 y 3935-3938: `R"(C:\Users\<USUARIO>\Documents\debug_cdda\npc_ai_pickup_v1_runtime.txt)"`.
- `npc_ai_spontaneous_runtime.txt` había crecido 296 líneas en el working tree, prueba de escritura en juego normal.
- Medición A/B: el subsistema de habla espontánea costaba 2,9-4,6 µs/llamada con logging y 0,4-0,8 µs sin él.

**DECISIÓN.** Un único gate `npc_ai::runtime_debug_enabled()`, apagado por defecto, activable con la variable de entorno `CDDA_NPC_AI_DEBUG=1`, más un override para tests. Sigue **exactamente** el patrón que el proyecto ya usaba en `profiling_enabled()` (`src/npc_ai_profiler.cpp` 72-83) y `world_event_jsonl_debug_enabled()` (`src/npc_ai_event_stream.cpp` 346-353).

**IMPLEMENTACIÓN.** `src/npc_ai_debug.h/.cpp`:
- `bool runtime_debug_enabled()` — atomic override + env var cacheada en static local.
- `void set_runtime_debug_enabled_for_test( bool )`.
- `class debug_stream` — sustituto directo de `std::ofstream`. Cuando el gate está apagado no abre nada, convierte a `false` y descarta toda inserción. Los sitios de llamada conservan su `if( debug ) { debug << ...; }` sin cambios.
- `void append_debug_line( filename, line )` — helper para los `debug_line()` de módulo.

**POR QUÉ.** `debug_stream` permite desactivar 20+ sitios de llamada sin reescribirlos, y hace que RAW vs CLEAN sea un **toggle en runtime del mismo binario**, que es un A/B mucho más limpio que comparar dos compilaciones.

**ARCHIVOS AFECTADOS.** `npc_ai_debug.h/.cpp` (nuevo contenido), `npcmove.cpp`, `npc_ai_spontaneous.cpp`, `npc_ai_batch_pickup.cpp`, `npc_ai_wield.cpp`, `npc_ai_pickup.cpp`, `npc_ai_action_parser.cpp`.

**RIESGOS.** Un módulo nuevo podría volver a escribir sin gate. Mitigación parcial: el test de higiene comprueba el gate, pero no detecta writers nuevos.

**VALIDACIÓN.** Test `npc_ai_diagnostic_logging_is_silent_unless_explicitly_enabled`. Verificación empírica: tras una corrida completa del banco con el gate apagado, los archivos bajo `test_user_dir/` quedan idénticos byte a byte y con el mismo timestamp; con `CDDA_NPC_AI_DEBUG=1` se actualizan.

**NOTA.** Además de gatear `debug_line()`, se envolvieron en `if( npc_ai::runtime_debug_enabled() )` los tres bloques `std::ostringstream` de `npc_ai_spontaneous.cpp` (INIT, SUPPRESSED_COOLDOWN, EVALUATE), para no construir la cadena cuando no se va a escribir.

---

### D3 — Reset de sesión de todos los mapas estáticos por NPC

**PROBLEMA.** El hallazgo más grave de la sesión, mayor de lo que decía la auditoría inicial.

**EVIDENCIA.** Hay **11** mapas estáticos de estado por NPC, todos indexados por `who.getID().get_value()`. `begin_ai_session()` / `end_ai_session()` (`src/npc_ai_async.cpp` ~676-690) solo limpiaban **2**.

| Módulo | Estado estático | Se limpiaba antes |
|---|---|---|
| `npc_ai_combat_social.cpp` 85 | `combat_states` | Sí |
| `npc_ai_memory.cpp` 45 | `recent_speech_by_npc` | Sí |
| `npc_ai_spontaneous.cpp` 128, 132 | `spontaneous_states`, `global_last_spoken_turn` | **No** |
| `npc_ai_goal.cpp` 12, 13 | `goals`, `next_goal_id` | **No** |
| `npc_ai_survival.cpp` 19 | `next_warmth_attempt` | **No** |
| `npc_ai_fire.cpp` 76 | `start_fire_tasks` | **No** |
| `npc_ai_vehicle_unload.cpp` 65 | `unload_tasks` | **No** |
| `npc_ai_batch_pickup.cpp` 67 | `food_batches` | **No** |
| `npc_ai_coordination.cpp` 22 | `assignments` | **No** |
| `npc_ai_equipment_memory.cpp` 31, 32 | `memories`, `loaded_npcs` | **No** |
| `npc_ai_watchlist.cpp` 26 | `watch_cache` | **No** |

**Impacto real.** Los id de personaje se reutilizan entre partidas. Cargar mundo A, volver al menú y cargar mundo B dejaba que los cooldowns, objetivos y tareas de un personaje se asignaran silenciosamente a otro personaje distinto. Además los mapas crecían durante toda la vida del proceso. `loaded_npcs` en `equipment_memory` era especialmente pernicioso: afirmaba que los datos de un id ya estaban cargados, impidiendo releerlos del NPC nuevo.

**DECISIÓN.** Cada módulo expone un `reset_all_*()` público; `begin_ai_session()` y `end_ai_session()` llaman a una única función `npc_ai::reset_all_ai_session_state()` que los invoca todos.

**IMPLEMENTACIÓN.** Se añadieron: `reset_all_spontaneous_states()`, `reset_all_goals()`, `reset_all_survival_state()`, `reset_all_start_fire_tasks()`, `reset_all_vehicle_unload_tasks()`, `reset_all_food_batches()`, `reset_all_assignments()`, `reset_all_equipment_memory_cache()`, `reset_watch_cache()`. Declaradas en sus cabeceras respectivas. `reset_all_ai_session_state()` está declarada en `src/npc_ai_async.h` para que los tests puedan usarla.

**POR QUÉ se eligió lista explícita y no un registro auto-suscrito.** Consistencia con el patrón que ya existía (`reset_all_combat_social_states()`, `reset_all_recent_speech()` ya se llamaban explícitamente), es greppable y evita sutilezas de orden de inicialización estática. AGENTS.md exige evolución incremental sobre reemplazo.

**RIESGOS.** Un módulo nuevo con estado estático puede olvidarse de registrarse. **Codex: al añadir cualquier mapa estático por NPC, añadir también su reset a `reset_all_ai_session_state()`.**

**VALIDACIÓN.** Test `npc_ai_per_npc_state_does_not_survive_a_session_change`. Antes del cambio ese test falla (goals y assignments sobreviven).

**IMPORTANTE.** `reset_all_equipment_memory_cache()` y `reset_watch_cache()` limpian **solo el espejo en memoria**. Los datos durables viven en las variables del NPC y en disco respectivamente, y se recargan bajo demanda. No hay pérdida de datos.

---

### D4 — Enumeración espacial de la percepción

**PROBLEMA.** `build_sensory_snapshot()` recorría **todas las casillas de todos los z-levels cargados** y descartaba después por distancia.

**EVIDENCIA.** `z_levels=21 × tiles_per_z=17424 = 365 904` candidatos por llamada, independientemente del radio pedido. Coste fijo medido ≈ 1 400 µs.

**DECISIÓN.** Usar `here.points_in_radius( origin, radio, fov_3d_z_range )`.

**IMPLEMENTACIÓN.** `src/npc_ai_perception.cpp`, bucle principal de `build_sensory_snapshot()`. Se conserva el filtro redondo `rl_dist(...) > effective_tile_scan_radius` porque `points_in_radius` devuelve un cubo. Se eliminó el nivel de anidamiento y se corrigió la indentación (que estaba inconsistente en el original).

**POR QUÉ.** El coste debe ser proporcional al radio pedido, no al tamaño del mapa.

**RIESGOS.** `points_in_radius` recorta a los límites del mapa, por lo que se eliminó la guarda manual `z < -OVERMAP_DEPTH || z > OVERMAP_HEIGHT`. Si Codex observa acceso fuera de rango, restaurar la guarda.

**VALIDACIÓN.** Radio 6: 2 954 → 1 323 µs. `[npc_ai]` verde. `tiles_returned` sin cambios (290), es decir mismo resultado con menos trabajo.

---

### D5 — Tope de radio en la escena detallada

**PROBLEMA.** `build_perception_context( who, detailed_scene )` pasaba radio `-1` (ilimitado, `MAX_VIEW_DISTANCE` = 60) cuando la consulta era de escena. Eso son 307 461 candidatos y **una prueba de línea de visión por casilla**, ~110 ms bloqueando el hilo principal.

**EVIDENCIA.** El renderizador nunca emite más de `detailed_ordinary_tile_render_limit` = 96 casillas ordinarias (`src/npc_ai_perception.cpp` 38). Se escaneaban 307 461 casillas para renderizar como mucho 96.

**DECISIÓN.** `detailed_prompt_tile_radius = 20`.

**IMPLEMENTACIÓN.** `src/npc_ai_perception.cpp`, `build_perception_context()`.

**POR QUÉ es seguro.** Las criaturas se recogen **por separado y a alcance de visión completo** (`g->get_creatures_if` con `who.sees`), así que las amenazas lejanas no se pierden. Solo se acota el escaneo de terreno e items.

**VALIDACIÓN DECISIVA.** Tras el cambio los bytes del prompt son **idénticos** (16 491 antes y después). Las 287 000 casillas adicionales no aportaban una sola letra al texto generado. Coste: 116 067 → 12 196 µs.

**RIESGOS.** Una casilla notable a más de 20 de distancia (un fuego lejano) ya no aparecería en el texto de terreno. Aceptado: "describe todo lo que ves" con radio 20 sigue siendo generoso, y las criaturas no están afectadas.

---

### D6 — Enrutado de consultas de percepción

**PROBLEMA.** `detailed_scene` no es solo un formato: cambia el radio de escaneo a ilimitado. El literal `"que ves"` estaba en `detailed_scene_query()`, así que la pregunta **más breve y más frecuente** disparaba el barrido exhaustivo.

**EVIDENCIA.** `"Que ves?"` medía 111 355 µs y `scene_query=1`.

**DECISIÓN.**
1. Quitar `"que ves"` y `"what do you see"` de `detailed_scene_query()`. Siguen siendo consulta sensorial normal (radio 12) vía `present_perception_query()`, que también contiene `"que ves"`.
2. `"ahora"` y `"actualmente"` ya no disparan percepción por sí solos: solo cuentan junto a un verbo de percepción (`ves`, `ver`, `hay`, `oyes`, `escuchas`, `hueles`).

**IMPLEMENTACIÓN.** `src/npc_ai_context.cpp`, `detailed_scene_query()` y `present_perception_query()`.

**POR QUÉ el punto 2.** Cualquier frase que mencionara el presente ("vámonos ahora", "ahora no") pagaba un escaneo sensorial completo.

**RIESGOS.** Alguna pregunta legítima en presente podría perder contexto de percepción. Es aceptable en Fase 0 y **queda subsumido por el Context Router de la Fase 3**, que debe rediseñar el enrutado de forma holística.

**VALIDACIÓN.** `"Que ves?"`: 111 355 → 4 446 µs. `"Vamonos ahora."`: `sensory_query=0`. `"Que hay aqui?"` sigue siendo detallada (hay un test existente que lo exige). `[npc_ai]` verde.

---

### D7 — RNG y protocolo de medición

**PROBLEMA.** Entre dos ejecuciones del banco los bytes de prompt variaban hasta 2,4× (11 678 → 27 377 para el mismo saludo), sin cambios de código en la construcción de prompts.

**EVIDENCIA / DIAGNÓSTICO.** No era estado acumulado en disco (`test_user_dir` no contiene archivos de memoria). Era la generación aleatoria del NPC: distinta semilla produce NPC con distinto inventario, rasgos y biografía, y eso cambia el tamaño del prompt.

**DECISIÓN.** Toda medición comparable usa `--rng-seed 1`.

**VALIDACIÓN.** Con semilla fija los bytes son **exactamente** reproducibles entre corridas consecutivas (9847 / 9765 / 9767 / 16475 / 16491 / 9865 idénticos). Los tiempos siguen variando ±5-10 % por ruido de máquina.

**CONSECUENCIA PARA CODEX.** Cualquier medición sin `--rng-seed 1` es inválida para comparar. Ver sección 22.

---

### D8 — Metodología RAW vs CLEAN

**DECISIÓN.** Aprovechar el gate de D2: RAW-equivalente = mismo binario con `CDDA_NPC_AI_DEBUG=1`; CLEAN = variable ausente. Mismo binario, misma semilla, único factor cambiado.

**POR QUÉ.** Comparar dos compilaciones distintas mezcla el efecto del logging con cualquier otra diferencia.

**SALVEDAD DOCUMENTADA.** El RAW-equivalente escribe en el directorio de usuario; el RAW **real** escribía además en la ruta absoluta de OneDrive desde `npc::pick_up_item()` en cada tick. El banco no ejerce pickup dirigido, así que la medición **subestima** el ahorro real durante un pickup.

---

## 4. ARCHIVOS MODIFICADOS

El checkpoint explícitamente solicitado `1e24bd1e52` consolidó 45 archivos del
trabajo de Fases 0-4. Después del checkpoint, el log trackeado
`npc_ai_spontaneous_runtime.txt` continúa modificado y fuera de alcance; no se
debe confundir con trabajo de estas fases. El working tree sigue siendo la
fuente de verdad para cambios posteriores.

### Archivos añadidos por este trabajo (ya trackeados)

| Archivo | Qué es | Estado | Requiere más trabajo |
|---|---|---|---|
| `npc_ai_metrics.md` | Registro de todas las mediciones RAW/CLEAN/por fase | Vivo, hay que seguir añadiendo | Sí, en cada fase |
| `tests/npc_ai_baseline_bench.cpp` | Banco de medición; tags ocultos baseline, Fase 6 y linterna | Funcional y ampliado | Ampliar con métricas de fases nuevas |
| `tests/npc_ai_hygiene_test.cpp` | 2 tests de Fase 0 | Completo | No |
| `tests/npc_ai_context_test.cpp` | Tests #5, #9 y #10 del Context Router | Completo | No |
| `NPC_AI_HANDOFF.md` | Este documento | — | — |

`tests/*.cpp` se recoge por comodín en `msvc-full-features/Cataclysm-test-vcpkg-static.vcxproj` línea 186 (`<ClCompile Include="..\tests\*.cpp" />`). **No hay que editar el .vcxproj para añadir tests.**

### Núcleo del gate de diagnóstico

| Archivo | Qué cambió | Por qué | Estado |
|---|---|---|---|
| `src/npc_ai_debug.h` | `runtime_debug_enabled()`, override de test, `debug_stream` y `append_debug_line()`; el stream mantiene el lock solo cuando está activo | D2 | Completo |
| `src/npc_ai_debug.cpp` | Atomic override + env `CDDA_NPC_AI_DEBUG`; escrituras serializadas para que worker y main thread no intercalen bloques | D2 | Completo |

### Sitios de logging gateados

| Archivo | Qué cambió | Estado |
|---|---|---|
| `src/npcmove.cpp` (+11/-...) | Dos `std::ofstream` con ruta OneDrive absoluta (`ai_request_pickup` ~3767, `pick_up_item` ~3935) sustituidos por `npc_ai::debug_stream`. Añadido `#include "npc_ai_debug.h"` | Completo |
| `src/npc_ai_spontaneous.cpp` (75 líneas) | `debug_line()` delega en `append_debug_line()`; 3 bloques `ostringstream` envueltos en el gate | Completo |
| `src/npc_ai_batch_pickup.cpp` (28) | `reset_debug()` y `debug_line()` gateados | Completo |
| `src/npc_ai_wield.cpp` (6) | `debug_stream` con truncado | Completo |
| `src/npc_ai_pickup.cpp` (5) | `debug_stream` con truncado | Completo |
| `src/npc_ai_action_parser.cpp` (7) | `debug_log()` gateado | Completo |

### Diagnóstico y corrección Qwen/Ollama B+D (cerrado y verificado)

| Archivo | Qué cambió | Estado |
|---|---|---|
| `src/npc_ai_client.h/.cpp` | Payload explícito con `system`, `options` (0.4/0.85/20/1.1/96), stops Qwen nativos y modelo `qwen3:14b` intacto; timestamps y trazas separadas de system/prompt/raw/extraída bajo el gate | Completo |
| `src/npc_ai_context.h/.cpp` | `npc_prompt_purpose`, construcción central de system por ruta, personalidad fuera del prompt, `OUTPUT_LANGUAGE` dinámico y validador de inglés corto reforzado | Completo |
| `src/npc_ai_async.h/.cpp` | Snapshot inmutable con `system_prompt`; worker transporta ambos campos, reintento FIFO corrige system+final de prompt; agregado separa bytes y tasa de idioma | Completo |
| `src/npc_ai_spontaneous.cpp`, `src/npc_ai_combat_social.cpp`, `src/npc_ai_action_parser.cpp`, `src/npc_ai_pickup.cpp`, `src/npc_ai_wield.cpp` | Reglas estáticas y personalidad retiradas de los prompts de usuario y centralizadas por propósito en `system` | Completo |
| `tests/npc_ai_async_test.cpp`, `tests/npc_ai_context_test.cpp`, `tests/npc_ai_combat_social_test.cpp`, `tests/npc_ai_conversation_test.cpp` | Contrato exacto, separación por rol, personalidad por NPC, parser, gate y regresiones; el fixture de conversación limpia sonidos globales y el caso grupal inyecta un sonido obsoleto para reproducir la contaminación de la suite completa | PASS |
| `tests/npc_ai_baseline_bench.cpp` | Métricas prompt/system y benchmark oculto de 12 llamadas reales de idioma | Completo; opt-in para Ollama real |

### Combat Social A+D+B+V (sesión 29/08/2026)

| Archivo | Qué cambió | Estado |
|---|---|---|
| `src/npc_ai_event_stream.h/.cpp` | Claim tipado (`FACT_ONLY/HIT_ONLY/LIMB_DISABLED/DEATH`), body part, daño, modo, lookup por sequence ID y JSONL | Completo; reutiliza el ring/lifecycle existente |
| `src/creature.cpp`, `src/melee.cpp`, `src/monster.cpp` | Hits normales de proyectil, Character y monster publican atacante/tirador, víctima, parte, daño y claim real | Completo + E2E monster melee |
| `src/activity_actor.cpp` | `heal_completed` conserva la parte tratada; actor y paciente ya estaban presentes | Completo |
| `src/npc_ai_client.h/.cpp` | `num_ctx=16384`, `num_predict=192`, `seed=1`, prompt/eval counts, detección de truncamiento y guarda pre-envío | Completo + contrato/test real |
| `src/npc_ai_async.h/.cpp` | Slots de utterance autoritativos en C++ y enqueue batch asíncrono | Completo; gameplay nunca espera Ollama |
| `src/npc_ai_combat_social.h/.cpp` | Cohortes por firma de conocimiento, prompt compartido seguro, parser/validador, scheduler de líneas, speak priority, registro emocional y métricas completas | Completo |
| `src/npc_ai_context.cpp` | Reglas permanentes de emoción y de grado de afirmación en system | Completo |
| `src/npc_ai_memory.h/.cpp` | Override de test para que benchmarks opt-in no escriban `.memory` ni SQLite | Completo; producción sigue persistiendo |
| `tests/data/npc_ai_combat_social_trace.json` | Traza fija de cinco hechos/cuatro segundos | Nuevo fixture reproducible |
| `tests/npc_ai_combat_social_test.cpp` | 5→1→4, cohortes sin fuga, expiración/scheduler y compatibilidad | PASS |
| `tests/npc_ai_event_stream_test.cpp` | Combate real: monster melee produce metadatos grounded | PASS |
| `tests/npc_ai_baseline_bench.cpp` | A/B RAW/NEW real con Qwen y métricas de grupo/speaker/cola/filtros | PASS opt-in |

### Reset de sesión

| Archivo | Qué cambió | Estado |
|---|---|---|
| `src/npc_ai_async.cpp` (+34) | Nueva `reset_all_ai_session_state()`; `begin/end_ai_session()` la llaman; 8 `#include` nuevos | Completo |
| `src/npc_ai_async.h` (+4) | Declaración de `reset_all_ai_session_state()` | Completo |
| `src/npc_ai_spontaneous.{h,cpp}` | `reset_all_spontaneous_states()` | Completo |
| `src/npc_ai_goal.{h,cpp}` | `reset_all_goals()` (también resetea `next_goal_id`) | Completo |
| `src/npc_ai_survival.{h,cpp}` | `reset_all_survival_state()` | Completo |
| `src/npc_ai_fire.{h,cpp}` | `reset_all_start_fire_tasks()` | Completo |
| `src/npc_ai_vehicle_unload.{h,cpp}` | `reset_all_vehicle_unload_tasks()` | Completo |
| `src/npc_ai_batch_pickup.{h,cpp}` | `reset_all_food_batches()` | Completo |
| `src/npc_ai_coordination.{h,cpp}` | `reset_all_assignments()`; `clear_assignments_for_test()` ahora delega en ella | Completo |
| `src/npc_ai_equipment_memory.{h,cpp}` | `reset_all_equipment_memory_cache()` (limpia `memories` y `loaded_npcs`) | Completo |
| `src/npc_ai_watchlist.{h,cpp}` | `reset_watch_cache()` | Completo |

### Percepción y enrutado

| Archivo | Qué cambió | Estado | Requiere más trabajo |
|---|---|---|---|
| `src/npc_ai_perception.cpp` (100 líneas) | `build_sensory_snapshot()` usa `points_in_radius`; se quitó un nivel de anidamiento y se normalizó la indentación; `build_perception_context()` usa `detailed_prompt_tile_radius = 20` | Completo | El coste por casilla (LOS) sigue siendo el dominante |
| `src/npc_ai_context.cpp` (+25) | `detailed_scene_query()` sin `"que ves"` / `"what do you see"`; `present_perception_query()` exige verbo de percepción junto a `"ahora"`/`"actualmente"` | Completo | Se rediseña en Fase 3 |

### Fase 1 — escalado de Combat Social

| Archivo | Qué cambió | Estado |
|---|---|---|
| `src/npc_ai_combat_social.cpp` | Sustituido el escaneo global por las listas de percepción ya calculadas por `npc::regen_ai_cache()`; presupuesto fijo de 12 criaturas / 24 LOS; huella grupal compartida por turno y reuso de criaturas validadas en polls ociosos; reset de la huella con la sesión | Completo |
| `src/npc_ai_combat_social.h` | Contador de comprobaciones LOS y límite público para test estable de complejidad | Completo |
| `src/npc.h`, `src/npc.cpp` | Accessors de solo lectura para hostiles y neutrales del `ai_cache` vanilla | Completo |
| `src/npcmove.cpp` | Los NPC neutrales visibles se incorporan a `ai_cache.neutral_guys`, igual que los monstruos neutrales | Completo |
| `tests/npc_ai_combat_social_test.cpp` | Nuevo test con 20 NPC que prueba la cota de trabajo por observador | Completo |

### Fase 2 — Self State

| Archivo | Qué cambió | Estado |
|---|---|---|
| `src/npc_ai_self.h` | Scope `physical_state/full_inventory`; observaciones de efectos, salud, moral y temperatura por parte | Completo |
| `src/npc_ai_self.cpp` | Lee fuentes vanilla, renderiza solo partes afectadas/efectos relevantes y omite inventario cuando la consulta no lo necesita | Completo |
| `src/npc_ai_context.cpp` | Eliminado `NECESIDADES FISICAS`; consultas propias usan snapshot directo sin escena; relación con jugador incluida | Completo; ampliar con router en Fase 3 |
| `tests/npc_ai_self_test.cpp` | Tests #6-#8: sin scan de escena, heridas/sangrado vanilla y stamina inmutable | Completo |

### Fase 3 — Context Router

| Archivo | Qué cambió | Estado |
|---|---|---|
| `src/npc_ai_context.h` | Enum público de 10 intenciones, clasificador y presupuesto por intención | Completo |
| `src/npc_ai_context.cpp` | Bloques por intención, límites duros, truncado UTF-8 explícito y ruta grupal de estado propio | Completo |
| `src/npc_ai_perception.h/.cpp` | `build_sensory_context()` separa escena de inventario/estado propio sin romper el wrapper legado | Completo |
| `tests/npc_ai_context_test.cpp` | Exclusión de contexto irrelevante, presupuestos y 20 prompts individuales | Completo |

### Fase 4 — Social Director (cerrada y verificada)

| Archivo | Qué cambió | Estado |
|---|---|---|
| `src/npc_ai_async.h/.cpp` | Presupuesto social compartido por clave de evento; normal=1, importancia ≥97=2; estado limpiado con la sesión | Completo |
| `src/npc_ai_combat_social.h/.cpp` | Identidad runtime estable de criatura y familias compartidas para coalescer observadores | Completo |
| `tests/npc_ai_async_test.cpp` | Gate de 2 reacciones importantes / 1 normal | PASS |
| `tests/npc_ai_combat_social_test.cpp` | Evento normal compartido entre dos NPC produce una request; gate no oculto con 20 observadores exige exactamente una | PASS |
| `tests/npc_ai_conversation_test.cpp` | Órdenes grupales deterministas dejan la cola LLM intacta | PASS |
| `tests/npc_ai_baseline_bench.cpp` | Aserción ≤1 y métricas 1/5/10/20 para evento normal compartido | PASS; 1/1/1/1 requests |

### Fase 6 — Equipment y órdenes grupales (cerrada y verificada)

| Archivo | Qué cambió | Estado |
|---|---|---|
| `src/npc_ai_equipment_memory.h/.cpp` | Adaptador explícito para drops involuntarios; reutiliza UID/owner, memoria persistente y recuperación existente | Completo |
| `src/character.cpp` | Lesión que impide empuñar memoriza el arma antes del drop tumbling | Completo |
| `src/melee.cpp` | Desarme físico memoriza el arma antes de añadirla al mapa | Completo |
| `src/monattack.cpp` | `bio_op_disarm` conserva el item, lo memoriza con su posición real y luego lo deja en el mapa | Completo |
| `src/npc_ai_equipment.h/.cpp` | Ejecución grupal determinista por NPC; verbos plurales; no crea requests LLM | Completo |
| `src/npctalk.cpp` | Ruta `selection.everyone` prueba equipment determinista antes de diálogo LLM | Completo |
| `tests/npc_ai_equipment_test.cpp` | Gates de melee, bio-op, lesión, mochila y propiedad grupal | PASS; 26/327 |
| `tests/npc_ai_baseline_bench.cpp` | Benchmark grupal 1/5/10/20 con cola LLM 0 | PASS |

### Pista de linterna (cerrada y verificada)

| Archivo | Qué cambió | Estado |
|---|---|---|
| `src/display.h/.cpp` | `active_light_indicator()` consulta el predicado cacheado `item::is_emissive` | Completo |
| `src/widget.h/.cpp` | Nueva variable de texto `active_light_text` | Completo |
| `data/json/ui/lighting.json` | Nuevo widget `active_light_desc` (`Light source: on/-`) | Completo |
| `data/json/ui/layout.json` | Indicador añadido al layout ambiental del sidebar | Completo |
| `tests/iuse_actor_test.cpp` | Las transiciones reales verifican actor, estado, nombre y mensaje on/off | PASS |
| `tests/widget_test.cpp` | El indicador sigue una fuente transportada al añadirla y retirarla | PASS |
| `tests/npc_ai_baseline_bench.cpp` | Benchmark cacheado de 100 000 consultas | PASS; 118 ns/consulta |

### No relacionado con esta tarea

`npc_ai_spontaneous_runtime.txt` (+296) — **ya estaba modificado antes de empezar la sesión**. Es un archivo de log trackeado. No lo toques y no lo commitees.

---

## 5. FUNCIONES Y PUNTOS EXACTOS PARA CONTINUAR

### Fase 1 (cerrada y verificada)

```
Archivo:    src/npc_ai_combat_social.cpp
Namespace:  npc_ai
Función:    build_combat_perception_snapshot_impl() / build_combat_perception_snapshot()
Declarada:  src/npc_ai_combat_social.h línea 146
Llamada:    process_combat_social( npc & ) (mismo archivo)
Hot path:   src/npcmove.cpp ~1365 (npc::move)
```
No retomar salvo regresión. Gate final: 20 NPC = 22 µs/NPC/turno frente a 16
con uno (1,375×); `[npc_ai]` 122/1517 verde. Ver secciones 6 y 13.

### Fase 2 (cerrada y verificada)

```
Archivo:    src/npc_ai_self.cpp / .h
Función:    build_self_snapshot( const npc &, self_snapshot_scope )
Consumidor: src/npc_ai_context.cpp, build_context_for_intent()
Tests:      tests/npc_ai_self_test.cpp
```

No retomar salvo regresión. `Como estas?` = 44 µs/3800 bytes y cero llamadas al
subsistema de percepción; `[npc_ai_self]` 7/62 y `[npc_ai]` 125/1539 verdes.

### Fase 3 (cerrada y verificada)

```
Archivo:    src/npc_ai_context.cpp
Funciones:  build_npc_system_prompt( const npc &, npc_prompt_purpose )
            build_npc_prompt( const npc &, const std::string &, npc_prompt_purpose )
Matchers:   present_perception_query() ~180, present_self_query() ~231, detailed_scene_query() ~255
Wrappers:   is_current_sensory_query() ~300, is_current_self_query() ~305, is_scene_inspection_query() ~310
```
No retomar salvo regresión. `classify_context_intent()` selecciona contexto y
`context_prompt_budget_bytes()` impone el límite. Gate grupal 20 NPC:
75 679 bytes / 2379 µs frente a 682 593 / 307 025 al inicio de fase.
Tras la separación B+D, el mismo caso suma 22 638 B de prompt + 26 321 B de
system = **48 959 B**; el presupuesto cuenta ambos campos.

### Fase 4 (cerrada y verificada)

```
Archivo:    src/npc_ai_combat_social.cpp  — process_combat_social(), notify_visible_enemy_killed() (~línea 164 en el .h)
Archivo:    src/npc_ai_spontaneous.cpp    — process_spontaneous_speech() ~1538-1587, maybe_enqueue_npc_reply() ~1938
Archivo:    src/npc_ai_async.cpp          — ai_request_queue, prioridades, dedup
Archivo:    src/npctalk.cpp               — ruta de diálogo grupal ~1373-1380, build_npc_prompt ~868
```

El gate central vive en `ai_request_queue::enqueue()` y
usa `social_event_key/social_reaction_budget`; Combat Social genera claves por
familia y sujeto físico. `clear_locked()` limpia el presupuesto con la sesión.
No se aplicó a `direct_dialogue`, por lo que `enqueue_group_ai_dialogue()` sigue
aceptando una request por NPC. Benchmark con semilla 1: 1/1/1/1 requests para
1/5/10/20 observadores; a 20, 76 499 µs totales. El gate no oculto
`combat_social_director_limits_a_shared_event_to_one_request_at_20_observers`
pasa y exige exactamente una request. Revalidación focal: 8 casos / 177
aserciones. Suite completa: 130/1723.

### Event Stream + Combat Social A/D/B/V (cerrado; no reconstruir)

```
Archivo:    src/npc_ai_event_stream.{h,cpp}
Funciones:  record_creature_world_event(), recent_world_events_for(),
            world_event_by_sequence(), reset_world_event_stream()
Batch:      try_enqueue_physical_batch(), build_combat_batch_prompt(),
            apply_combat_batch_completion(), emit_due_combat_line()
Prioridad:  combat_social_speak_priority()
Métricas:   combat_social_metrics_snapshot(), reset_combat_social_metrics()
Async:      enqueue_combat_social_batch_dialogue(), ai_combat_utterance_slot
Lifecycle:  reset_all_ai_session_state() -> reset_world_event_stream() y
            reset_all_combat_social_states()
Tests:      combat_social_batches_fixed_five_fact_trace_into_four_scheduled_lines
            combat_social_batch_prompt_contains_only_facts_shared_by_its_cpp_cohort
            npc_ai_real_monster_melee_records_grounded_hit_metadata_end_to_end
```

No suscribirse a `event_bus`: los hooks directos existentes dan participación,
vista/oído y outcome físico. El enum contiene 39 tipos. Productores directos:
`npc_attack`, `npc_grabbed`, `weapon_jammed`, `failed_escape`, `grab_broken`,
`dragged`, `significant_critical`, `attack_missed`, `dodge`, `ally_saved`,
`heal_started`, `heal_completed` y `enemy_killed`; el snapshot produce los
estados/transiciones restantes. `ally_dragged`, `ally_critical_hit` y
`player_critical_hit` son aliases relativos alcanzables, no hechos muertos.

Guardas permanentes: no enviar un prompt fuera del presupuesto; no aceptar una
respuesta con `context_truncated`; cada evento del candidato debe existir, no
estar vencido y pertenecer tanto al slot como a `known_by_npc_ids` del speaker.
Speaker/priority/expiry/audience se derivan en C++ del subconjunto final de
event IDs. El fallback es una sola inferencia de un hecho/slot y solo con cola
menor que 3.

### Fase 6 (cerrada y verificada)

```
Adaptador:  src/npc_ai_equipment_memory.cpp — remember_involuntary_weapon_drop()
Hooks físicos recuperables (NO hay un único hook, ver secciones 11 y 22.3):
  src/character.cpp  ~8315       lesión/tumbling antes de put_into_vehicle_or_drop()
  src/melee.cpp      ~1924-1929 desarme, remove_weapon() + add_item_or_charges()
  src/monattack.cpp  ~4370-4380 bio_op_disarm, i_rem() + add_item_or_charges()
Grupo:      src/npc_ai_equipment.cpp — execute_group_equipment_command()
Ruta UI:    src/npctalk.cpp — selection.everyone antes de enqueue_group_ai_dialogue()
```

No se enganchó `Character::remove_weapon()` globalmente: también se usa para
transferencias voluntarias y destrucción. `melee.cpp` ~2336 corresponde al
shatter de vidrio y no deja un arma recuperable. Gates: equipment 26/327,
nuevos F6 4/82, benchmark a 20 = 3212 µs / 160 µs por NPC / cola LLM 0;
suite completa 134/1810.

### Fase 7

```
Archivo:    src/npc_ai_database.cpp
  ~70-75    ruta de la base de datos
  ~156-159  pragmas actuales
  ~262-334  esquema
  ~388-400  índices
  ~428-535  lecturas
  ~538-664  escrituras (~546-569 en particular)
Archivo:    src/npc_ai_memory.cpp  ~267-293 (lectura legacy de archivo), ~579-621 (escritura legacy)
```

### Pista de linterna (cerrada y verificada)

```
Mensajes:   data/json/items/tool/lighting.json + src/iuse_actor.cpp
Detección:  src/display.cpp — active_light_indicator()
Widget:     src/widget.h/.cpp — active_light_text
JSON UI:    data/json/ui/lighting.json — active_light_desc
Layout:     data/json/ui/layout.json — light_moon_wind_temp_layout
Tests:      tests/iuse_actor_test.cpp + tests/widget_test.cpp
Benchmark: tests/npc_ai_baseline_bench.cpp — npc_ai_flashlight_sidebar_indicator_cost
```

No se cambió `iuse_transform`: cada actor ya cargaba y emitía el texto correcto.
La regresión captura el log antes de entrar en un `THEN`, porque el listener de
Catch2 limpia mensajes al iniciar cada sección. Para el indicador no se usa
`LIGHT_300`: `Item_factory::finalize_pre()` convierte todos los flags `LIGHT_n`
en `itype::light_emission` y los elimina. La ruta correcta es
`cache_has_item_with( "item::is_emissive", &item::is_emissive )`, que conserva
el caché vanilla y cubre cualquier fuente de luz transportada. Foco final:
3 casos / 66 aserciones; 100 000 consultas = 11 837 us (118 ns/consulta).

### Diagnóstico y corrección Qwen/Ollama B+D (cerrado y verificado)

```
Contrato:   src/npc_ai_client.cpp — build_ollama_request_json(),
            ollama_request_parameters_summary(), parse_ollama_response_json()
System:     src/npc_ai_context.cpp — build_npc_system_prompt(),
            dialogue_language_retry_instruction()
Latencia:   src/npc_ai_async.cpp — log_request_latency(),
            reset_latency_diagnostics()
Snapshot:   src/npc_ai_async.h — ai_request_snapshot::system_prompt/prompt
Salida:     <user_dir>/npc_ai_ollama_diagnostics.txt
Gate:       CDDA_NPC_AI_DEBUG=1
Tests:      tests/npc_ai_async_test.cpp — [npc_ai_ollama]
Live A/B:   tests/npc_ai_baseline_bench.cpp — [.npc_ai_ollama_live]
```

El archivo de salida no se abre con el gate desactivado. Cada request real deja
el contrato efectivo, system/prompt finales separados y las respuestas
raw/extraída; al aplicar
la completion se añaden una línea por request y otra `NPC_AI_LATENCY_SUMMARY`
acumulada desde el último `begin/end_ai_session()`. Los ejecutores falsos de
tests conservan compatibilidad: si no devuelven timestamps, la cola usa el fin
del executor como fallback; los executors antiguos de un argumento se adaptan
sin perder el system en producción. Gate final: 2 casos / 34 aserciones; suite
`[npc_ai]` 138/1912 con semilla 1. La muestra real bajó los reintentos de idioma
de 4/12 a 3/12 y repitió 3/12, sin fallos finales. Ver sección 19.

---

## 6. FASE 1 — DETALLE

### Ya implementado

1. **Enumeración por radio** (D4). `build_sensory_snapshot()` usa `points_in_radius( origin, effective_tile_scan_radius, fov_3d_z_range )` en lugar de recorrer `points_on_zlevel(z)` para 21 niveles.
2. **Tope de escena detallada** (D5). `detailed_prompt_tile_radius = 20` en `build_perception_context()`.
3. **Probado y revertido**: corto-circuito de notabilidad (sección 22).
4. **Snapshot de combate acotado**: candidatos desde `npc::ai_cache`, máximo 24 LOS.
5. **Reuso ocioso seguro**: lista visual por observador reutilizada solo mientras
   no cambie la huella grupal de estado físico.

### Límites actuales conocidos

- El coste por casilla restante está dominado por `who.sees( here, position )`, no por la construcción de nombres. Esto se **demostró** al medir que evitar la construcción de la observación no cambió nada.
- No hay caché de snapshot sensorial por NPC. Aclaración 3 del usuario: una caché **solo** vale si hay reutilización real dentro del mismo turno, y **nunca** debe compartirse entre NPC distintos, porque posición, z-level, línea de visión y radio son propios del observador.

### O(N²) de Combat Social — corregido

`build_combat_perception_snapshot( who )` en `src/npc_ai_combat_social.cpp` ~991-1037 recorre **todas las criaturas visibles** para cada observador y, para cada una, calcula datos derivados (`adjacent_hostiles`, `target_name`, `targeting_observer`, condición, etc.). Con N NPC aliados, cada uno ve a los otros N-1, así que el coste por llamada crece linealmente con N y el coste por turno crece con N².

Medición (BASELINE CLEAN, semilla 1, mapa vacío, sin combate):

| NPC | `npc_ai_combat_snapshot` µs/llamada | hot path µs/turno |
|---|---|---|
| 1 | 6,35 | 12 |
| 5 | 35,35 | 82 |
| 10 | 80,09 | 294 |
| 20 | 176,60 | 990 |

El coste **por NPC por turno** crecía de 12 a 49 µs. Era el componente dominante del hot path.

### Implementación final

1. `npc::ai_cache` queda como fuente de candidatos ya clasificados por vanilla;
   se añadieron accessors de solo lectura para hostiles y neutrales.
2. Cada snapshot conserva LOS propia del observador y un presupuesto fijo de
   12 criaturas visibles / 24 comprobaciones. No existe caché visual compartida.
3. Los polls ociosos periódicos reutilizan la última lista validada. Una huella
   compartida por turno de HP/sangrado/agarre/retirada invalida ese reuso y
   fuerza snapshot completo cuando cambia un estado socialmente relevante.
4. La huella forma parte del estado de sesión y se limpia explícitamente.

### Invariantes que deben conservarse

- Un enemigo tras una pared opaca **no** debe aparecer en el snapshot. Test existente: `combat_social_snapshot_contains_only_visible_creatures`, sección "hostile behind opaque wall is absent".
- El prompt **nunca** debe nombrar a un enemigo oculto. Test: `combat_social_prompt_does_not_name_hidden_enemy`.
- Los cooldowns son **por NPC**, no globales. Test: `combat_social_cooldowns_remain_per_npc_instead_of_globally_muting_the_group`.
- El snapshot se reutiliza dentro del mismo turno. Test: `combat_social_reuses_snapshot_within_turn_and_polls_idle_periodically`.
- Al cierre de Fase 1, dos NPC que veían el mismo zombi podían encolar ambos.
  Fase 4 ya resolvió ese fan-in sin alterar el diseño de percepción de Fase 1.

### Test y gate final

`combat_social_snapshot_work_is_bounded_per_observer_at_20_npcs` prueba una
cota estable de trabajo. Benchmark con semilla 1: 16/16/23/22 µs por NPC y
turno para 1/5/10/20 NPC; 20/1 = 1,375×, menor que el gate de 1,5×.

---

## 7. FASE 2 — SELF STATE

**Objetivo.** Que CDDA sea la fuente de verdad del estado del NPC, y **reducir** datos irrelevantes en lugar de añadir más. Aclaración 7 del usuario, literal: *"No quiero simplemente agregar más datos de Character al prompt. Quiero REDUCIR datos irrelevantes."*

**Punto de entrada.** `src/npc_ai_self.cpp`, `build_self_snapshot()` ~90-185.

**Duplicación a eliminar.** `src/npc_ai_context.cpp` ~517-525 emite un bloque "NECESIDADES FISICAS" con `who.get_hunger()`, `get_thirst()`, `get_sleepiness()`, `get_perceived_pain()` **además** de lo que ya emite `render_self_snapshot()`. Hay que unificar en una sola fuente.

**Para "¿cómo estás?", "¿cómo te sientes?", "¿estás herido?", "¿dónde te duele?" priorizar:**

- HP real por parte del cuerpo
- Partes corporales dañadas
- Sangrado
- Dolor
- Hambre
- Sed
- Fatiga
- Temperatura corporal (`get_part_temp_cur`, ya se usa en `npc_ai_survival.cpp`)
- Efectos activos relevantes
- Estado físico relevante
- Moral **solo si se confirma que existe y es significativa para NPC** — verificar antes de incluirla
- Personalidad (`who.personality`)
- Relación con el jugador
- Memoria conversacional relevante

**NO escanear la escena completa** salvo que haya otro motivo real. Hoy sí se hace (ver Fase 3).

**PROHIBICIÓN EXPLÍCITA.** No modificar la mecánica de stamina existente. Solo **lectura**. Hay sincronización WALK/RUN de seguidores en `src/avatar.cpp` ~1302-1307 que no debe romperse.

### Implementación final

- `build_self_snapshot()` acepta scope `physical_state` o `full_inventory`.
  El primero no recorre inventario y se usa para preguntas corporales.
- La fuente de verdad son APIs vanilla: HP global y por parte, sangrado,
  mordida/infección/rotura, dolor, hambre, sed, fatiga, temperaturas, stamina,
  moral y efectos que CDDA marca para mostrar en información.
- El render solo enumera partes dañadas o afectadas; no llena el prompt con
  partes sanas. La relación con el jugador y personalidad siguen derivándose
  directamente de `npc`.
- `build_current_state_context()` evita por completo `build_perception_context()`
  para consultas propias. Solo consultas explícitas de inventario usan el scope
  completo. Se eliminó el bloque duplicado `NECESIDADES FISICAS`.
- No se modificó ninguna mecánica de stamina, WALK/RUN ni actividad.

Gate con semilla 1: `Como estas?` pasó de 4479 µs/9765 bytes al cierre de
Fase 1 a **44 µs/3800 bytes** (101,8× y −61 %); `Estas herido?` mide
50 µs/3802 bytes. `[npc_ai_self]` 7 casos/62 aserciones y suite completa
125/1539. Tests #6-#8 en PASS. **Fase 2 cerrada.**

---

## 8. FASE 3 — CONTEXT ROUTER

**Problema medido.** `build_npc_prompt()` produce ~9,8 KB para cualquier consulta, incluido "Hola.", y llama a `build_perception_context()` incondicionalmente (~4,4 ms). Unos 4,5 KB del prompt son instrucciones estáticas repetidas en cada petición (`src/npc_ai_context.cpp` líneas ~454-504).

**Escalado grupal medido (semilla 1):**

| NPC | bytes totales | bytes/NPC |
|---|---|---|
| 1 | 9 853 | 9 853 |
| 5 | 46 329 | 9 266 |
| 10 | 331 052 | 33 105 |
| 20 | 680 551 | 34 028 |

Entre 5 y 10 NPC los bytes por NPC se multiplican por 3,6: cada prompt describe a los demás. Con 20 NPC, una sola frase del jugador genera **680 KB** de prompt.

**Diseño previsto:**

- **Clasificar la intención** del turno: saludo, estado propio, percepción breve, percepción exhaustiva, memoria, orden determinista, combate, social. Sustituye a los matchers ad hoc actuales.
- **Contexto obligatorio vs opcional** por intención. Estado propio no necesita escena; percepción no necesita el historial completo de conversación.
- **Presupuesto de tokens/bytes** por petición, con degradación explícita cuando hay muchos NPC.
- **Deduplicación**: no repetir el mismo bloque de instrucciones estáticas si puede ir en el system prompt en lugar del user prompt.
- **Caché**: solo donde haya reutilización real dentro del turno, y respetando Aclaración 3 (datos por observador nunca compartidos entre NPC).
- **Límite duro** al número de NPC descritos dentro del prompt de otro NPC.

**Resultado esperado según el usuario:** menos datos irrelevantes debería producir respuestas más coherentes, menos palabras absurdas, menos mezcla contextual, prompts más pequeños y menor latencia. Es tanto una corrección de rendimiento como de **calidad de lenguaje**.

### Implementación final

- `context_intent` clasifica saludo, estado propio, inventario, percepción breve,
  percepción detallada, memoria, vigilancia, habla espontánea, NPC-a-NPC y general.
- Cada intención construye solo su contexto. `build_sensory_context()` permite
  percepción sin añadir inventario/estado propio; las rutas internas que sí
  requieren escena la conservan explícitamente.
- `context_prompt_budget_bytes()` define límites de 8-24 KiB y
  `bounded_context()` degrada contexto opcional con marcador explícito y corte
  seguro de UTF-8. La frase del jugador se conserva al final del prompt.
- `Como estan?` se trata como estado propio: en diálogo grupal cada NPC responde
  desde su cuerpo, personalidad y relación, sin describir a todos los demás.

Gate semilla 1: 20 NPC, **682 593 → 75 679 bytes** (−88,9 %) y
**307 025 → 2379 µs** (129×). Saludo 37 µs/2963 B; orden general 40 µs/2972 B;
escena breve 6662 µs/6201 B; detallada 17 372 µs/13 240 B. Suite
`[npc_ai_context]` 3/130 y completa 128/1669. Tests #5, #9 y #10 en PASS.
**Fase 3 cerrada.**

---

## 9. FASE 4 — SOCIAL DIRECTOR

**Problema medido.** Relación 1:1 entre observadores y peticiones al LLM:

| observadores | requests encolados | profundidad de cola |
|---|---|---|
| 1 | 1 | 1 |
| 5 | 5 | 5 |
| 10 | 10 | 10 |
| 20 | **20** | 20 |

Un solo hecho físico (un zombi visible) genera 20 peticiones.

### Tres rutas que NO deben mezclarse (Aclaración 4, crítica)

**A) Diálogo grupal iniciado por el jugador** — "Talk to everyone" / "Hablar con todos".
**NO** aplicar fan-in ni portavoz. Si el jugador pregunta "¿cómo están?" a 5 NPC seleccionados, **cada NPC conserva la posibilidad de responder individualmente** usando su personalidad, estado corporal, heridas, memoria, relación y contexto.

**B) Reacción social a evento del mundo** — muere un zombi, lo ven 10 NPC.
Aquí **sí** actúa el Social Director: normalmente 0 o 1 portavoz. Aplica a habla espontánea, combate social, reacción a eventos, NPC-to-NPC espontáneo, chatter ambiental y eventos del mundo.

**C) Orden determinista grupal** — "recojan sus armas", "suelten sus mochilas", "síganme", "protejan esta posición".
**No generar respuestas LLM innecesarias** si la orden puede interpretarse y ejecutarse directamente. Debe ejecutarse **por objetivo** (cada NPC actúa sobre SU propio objeto).

### Presupuesto social (Aclaración 6)

"1 evento → 1 portavoz" es la regla **normal, no absoluta**. Debe existir presupuesto:

- evento normal: 0-1 reacción
- evento importante: 0-2 reacciones
- evento excepcional: cantidad limitada por presupuesto

Casos que justifican más de una reacción: muerte de un compañero importante, NPC gravemente herido, jugador incapacitado, amenaza extraordinaria, conflicto entre NPC, evento extremadamente significativo. Incluso entonces, **nunca N peticiones para N observadores**.

El objetivo no es silenciar al grupo artificialmente, es evitar tormentas de LLM manteniendo naturalidad.

### Otros elementos

Cooldowns contextuales (ya existen por NPC, conservarlos), personalidad, prioridad, interrupciones, fairness entre NPC en la cola (`ai_request_queue` ya tiene 6 carriles de prioridad y `fairness_burst_limit = 8`).

---

## 10. FASE 5 — EVENT DRIVEN

**Migración prevista.** De evaluación continua por turno hacia consumo de eventos.

**Aclaración 5 del usuario, obligatoria.** Toda suscripción debe tener ownership y lifecycle explícitos. **Antes de implementar subscribers permanentes, investigar cómo maneja CDDA el lifecycle de `event_bus` en esta versión.**

Escenario a evitar:
```
cargar partida A → registrar listener
volver al menú   → el listener permanece
cargar partida B → registrar listener nuevo
evento           → callback A + callback B
```

**Prueba obligatoria cuando la infraestructura lo permita:**
1. iniciar juego, 2. cargar partida, 3. generar evento, 4. volver al menú, 5. cargar otra partida, 6. generar evento, 7. comprobar que se procesa **una** sola vez, 8. cambiar de mundo, 9. comprobar de nuevo, 10. cerrar juego.

**No permitir:** listeners duplicados, callbacks de sesión anterior, handlers vivos tras destruir `game`, referencias a NPC destruidos, referencias a `map`/`game` fuera de lifecycle, eventos procesados dos veces, almacenamiento estático que sobreviva a la sesión.

**Contrato asíncrono, sigue vigente.** Los callbacks de `event_bus` que leen mundo o `Character` deben hacerlo desde el hilo correcto y convertir la información **inmediatamente** en snapshots, ids o eventos por valor. El worker de Ollama **sigue sin poder acceder** a `npc`, `Character` vivo, `map`, `game` ni UI. Está documentado en `src/npc_ai_async.h` línea 130: *"Thread-safe transport only. It never reads or writes CDDA world state."*

**Si la arquitectura vanilla de `event_bus` ya gestiona todo esto de forma segura, reutilizarla. No construir un segundo sistema paralelo de eventos.**

Estado actual: existe `src/npc_ai_event_stream.{h,cpp}` con `reset_world_event_stream()` y un log JSONL opcional gateado por `CDDA_NPC_AI_EVENT_DEBUG`. Coste medido de publicación: 2,19 µs/evento.

---

## 11. FASE 6 — EQUIPMENT Y ÓRDENES

### Corrección importante de la auditoría inicial

Una conclusión temprana decía que `Character::put_into_vehicle_or_drop` sería un punto único donde enganchar todos los drops involuntarios de arma. **Es FALSO.** Al reverificar el código:

| Sitio | Mecanismo real |
|---|---|
| `src/character.cpp` ~8315 | Sí usa `put_into_vehicle_or_drop()` — solo el caso de caída/tropiezo |
| `src/melee.cpp` ~1924-1929 | Usa `remove_weapon()` + `here.add_item_or_charges()` para el desarme recuperable |
| `src/monattack.cpp` ~4370-4376 | Usa `foe->i_rem()` + `here.add_item_or_charges()` |

Es decir, **un solo hook es insuficiente**. Hay que engancharse en los tres sitios reales, o encontrar un punto común más abajo.

Nota verificada al implementar: `src/melee.cpp` ~2336 elimina un arma de vidrio
que se ha hecho añicos; no añade un arma recuperable al mapa y no debe crear
memoria de recuperación.

### Qué debe cubrir la fase

- Arma perdida en combate (desarme por melee, desarme por bio-op, caída)
- Recoger el arma, soltar el arma
- Mochila: soltar, recuperar, **volver a vestir**
- `equipment_memory`: `remember_dropped_equipment()` (`src/npc_ai_equipment_memory.cpp` ~208), `visible_item_with_uid()` (~195-200), recuperación en `process_equipment_recovery()`
- Órdenes individuales y **grupales**
- "recoge tu arma", "recoge tu mochila", "suelta tu mochila"
- Chat grupal
- **El NPC correcto debe identificar SU propio objeto**, no el de otro. Es la razón de que exista el uid en las variables del NPC (`npc_ai_equipment_uid`, `npc_ai_equipment_owner`).

Las órdenes grupales de este tipo son la ruta **C** de la sección 9: deterministas, ejecutadas por objetivo, sin generar peticiones LLM innecesarias.

**Preservar las implementaciones que ya funcionan.** `request_equipment_recovery()`, `process_equipment_recovery()` y el flujo de wear/pickup existentes están operativos.

### Implementación final

- `remember_involuntary_weapon_drop()` es un adaptador pequeño llamado desde
  los tres sitios recuperables; marca UID/owner y `retrieval_expected=true`.
- No existe hook en `remove_weapon()` global, evitando memorias falsas por
  transferencias, unwield voluntario o destrucción.
- `execute_group_equipment_command()` ejecuta cada orden sobre el inventario y
  la memoria del NPC objetivo. `npctalk.cpp` la invoca antes de la cola LLM.
- Los plurales `suelten` y `recojan` se reconocen determinísticamente.
- Tests #19-#22 pasan; recuperación de mochila conserva contenido y vuelve a
  vestir el mismo UID. Benchmark 20 NPC: 3212 µs, 160 µs/NPC, cola LLM 0.

---

## 12. FASE 7 — SQLITE Y MEMORIA

### Aclaración 2 del usuario — cambio de fase

Los pragmas `journal_mode=WAL` y `synchronous=NORMAL` estaban en la Fase 0 del prompt maestro. **El usuario los movió explícitamente a la Fase 7** y prohibió tratarlos como cambios triviales aislados. **No se aplicaron en esta sesión.**

Motivo: actualmente se hacen múltiples ciclos `sqlite3_open` / `prepare` / `finalize` / `close`, y el lifecycle definitivo de la conexión todavía va a cambiar. Los pragmas deben aplicarse como parte de un diseño coherente.

### Arquitectura encontrada

- `src/npc_ai_database.cpp`: ruta de DB ~70-75, pragmas ~156-159, esquema ~262-334, índices ~388-400, lecturas ~428-535, escrituras ~538-664.
- La DB vive **por mundo**: `save/<Mundo>/npc_ai_memory/npc_ai.db`. Verificado en disco: existe `save/Melipilla/npc_ai_memory/npc_ai.db`.
- Existen backups de migraciones previas en `save-backup-20260826-220955/Reece City/npc_ai_memory/` con sufijos `.before-dedup-`, `.before-history-`, `.before-item-id-backfill-`, `.before-world-history-`. **Confirma que ya hubo migraciones y que la compatibilidad con DB existentes es un requisito real.**
- Persistencia paralela en archivos: `src/npc_ai_memory.cpp` ~267-293 (lectura legacy) y ~579-621 (escritura legacy), `src/npc_ai_world_memory.cpp` ~222, `src/npc_ai_watchlist.cpp` ~166.

### Qué NO migrar innecesariamente

La watchlist y la memoria de mundo en archivo funcionan. No convertirlas a SQLite solo por uniformidad. AGENTS.md: preferir evolución incremental.

### Modelo previsto

Conexión persistente por mundo, statements preparados cacheados, transacciones para escrituras en lote, índices revisados, WAL y `synchronous` elegidos conscientemente, lifecycle explícito de carga/descarga de mundo, política de retención y limpieza, migración compatible.

### Criterios de cierre de la Fase 7

WAL confirmado; `synchronous` elegido conscientemente; apertura/cierre de mundo correcto; cambio de partida correcto; cierre limpio; ninguna corrupción; compatibilidad con DB existente.

**Antes de cambiar pragmas: verificar cómo se aplican a las conexiones reales actuales.**

---

## 13. PERFORMANCE

Todas las cifras con `--rng-seed 1`, build Release x64, Windows 10.0.26200, MSVC (VS 18 Community). Los bytes son exactamente reproducibles; los tiempos varían ±5-10 %.

### BASELINE RAW vs CLEAN — A/B controlado, mismo binario

RAW-equivalente = `CDDA_NPC_AI_DEBUG=1`; CLEAN = variable ausente.

**Subsistema de habla espontánea (`npc_ai_spontaneous`, µs/llamada):**

| NPC | logging ON | logging OFF | reducción |
|---|---|---|---|
| 1 | 4,56 | 0,76 | 6,0× |
| 5 | 3,66 | 0,43 | 8,5× |
| 10 | 3,68 | 0,41 | 9,1× |
| 20 | 3,13 | 0,57 | 5,5× |

> El valor citado por el usuario, **2,9 µs → 0,4 µs**, es correcto y corresponde a la franja de 5-20 NPC.

**Hot path completo (µs/turno):**

| NPC | ON | OFF | mejora |
|---|---|---|---|
| 1 | 16 | 11 | 31 % |
| 5 | 102 | 82 | 20 % |
| 10 | 325 | 273 | 16 % |
| 20 | 1 063 | 990 | 7 % |

El logging pesa mucho en el subsistema espontáneo pero poco en el total, porque domina `build_combat_perception_snapshot`.

### BASELINE CLEAN de referencia

| métrica | valor |
|---|---|
| percepción radio 6 / 12 / 20 / 60 | 2 954 / 5 832 / 14 452 / 114 062 µs |
| casillas relevantes devueltas (cualquier radio) | 290 |
| candidatos recorridos por llamada | 365 904 |
| prompt saludo | 9 847 B / 13 162 µs |
| prompt "¿cómo estás?" | 9 765 B / 6 021 µs |
| prompt "¿qué ves?" | 16 475 B / 111 355 µs |
| prompt grupal 20 NPC | 680 551 B / 247 416 µs |
| hot path 1/5/10/20 NPC | 12 / 82 / 294 / 990 µs por turno |
| combat_snapshot 1/5/10/20 NPC | 6,35 / 35,35 / 80,09 / 176,60 µs |
| requests LLM por evento, 20 observadores | 20 |

### Estado al cerrar Fase 1

| métrica | CLEAN | actual | mejora |
|---|---|---|---|
| "Que ves?" | 111 355 µs | **4 446 µs** | 25× |
| "Describe todo lo que ves" | 116 067 µs | **12 196 µs** | 9,5× |
| "Como estas?" | 6 021 µs | 4 479 µs | 1,34× |
| percepción radio 6 | 2 954 µs | 1 351 µs | 2,2× |
| percepción radio 12 | 5 832 µs | 4 621 µs | 1,26× |
| prompt grupal 20 NPC | 247 416 µs | 209 381 µs | 1,18× |

Bytes de prompt actuales: saludo 9 847, self 9 765, "Que ves?" 9 762, detallada 16 491, grupal 20 NPC 680 551.

### Fase 1 final — escalado del hot path

Comando: `npc_ai_baseline_hot_path_scaling --rng-seed 1 -d yes`.

| NPC | CLEAN hot path µs/turno | Fase 1 µs/turno | Fase 1 µs/NPC/turno | combat_snapshot Fase 1 µs |
|---|---:|---:|---:|---:|
| 1 | 12 | 16 | 16 | 7,50 |
| 5 | 82 | 83 | 16 | 7,25 |
| 10 | 294 | 231 | 23 | 14,69 |
| 20 | 990 | **456** | **22** | **14,15** |

El coste por NPC con 20 es 1,375× el de un NPC, dentro del gate de 1,5×.
Frente a CLEAN, el hot path de 20 NPC baja 54 % (990 → 456 µs/turno) y el
snapshot medio baja 12,5× (176,60 → 14,15 µs). La reconstrucción periódica se
conserva, pero en estado ocioso reutiliza la lista ya validada; una huella grupal
por turno fuerza reconstrucción completa al cambiar HP, sangrado, agarre o retirada.

Regresión final Fase 1: `[npc_ai_combat_social]` 21/356 y `[npc_ai]` 122/1517,
todo pasa con semilla 1.

### Fase 2 final — Self State relevante

Comando: `npc_ai_baseline_prompt_cost_by_query_type --rng-seed 1 -d yes`.

| consulta | Fase 1 | Fase 2 | bytes Fase 1 | bytes Fase 2 | resultado |
|---|---:|---:|---:|---:|---|
| `Como estas?` | 4479 µs | **44 µs** | 9765 | **3800** | 101,8×; −61 % bytes |
| `Estas herido?` | — | **50 µs** | 9767 | **3802** | snapshot corporal directo |

La consulta propia registra cero llamadas a `profile_subsystem::perception` y
no recorre inventario salvo que la frase lo pida. Los demás tipos continúan
siendo responsabilidad de Fase 3; medición de referencia actual: saludo
13 518 µs/10 459 B, escena breve 5645 µs/10 374 B, detallada
15 785 µs/17 413 B, memoria 8569 µs/10 477 B.

Regresión final Fase 2: `[npc_ai_self]` 7/62 y `[npc_ai]` 125/1539, todo pasa
con semilla 1.

### Fase 3 final — Context Router y presupuesto

Comando conjunto con semilla 1:
`npc_ai_baseline_prompt_cost_by_query_type,npc_ai_baseline_group_prompt_scaling`.

| consulta | inicio F3 avg_us/bytes | final avg_us/bytes | resultado |
|---|---:|---:|---|
| saludo | 13 518 / 10 459 | **37 / 2963** | sin escena; 365× |
| estado propio | 44 / 3800 | 56 / 3800 | misma ruta acotada; ruido temporal |
| escena breve | 5645 / 10 374 | 6662 / **6201** | solo percepción; −40 % bytes |
| escena detallada | 15 785 / 17 413 | 17 372 / **13 240** | solo percepción; −24 % bytes |
| memoria | 8569 / 10 477 | 8296 / **3069** | sin escena; −71 % bytes |
| orden/general | 8333 / 10 468 | **40 / 2972** | sin escena; 208× |

| NPC | inicio F3 total_us/bytes | final total_us/bytes | final µs/NPC |
|---:|---:|---:|---:|
| 1 | 35 821 / 10 465 | 80 / 3780 | 80 |
| 5 | 55 241 / 47 250 | 370 / 18 911 | 74 |
| 10 | 140 981 / 332 072 | 700 / 37 823 | 70 |
| 20 | 307 025 / 682 593 | **2379 / 75 679** | 118 |

El total de 20 NPC queda por debajo del presupuesto agregado de 160 KiB y cada
prompt conserva una respuesta individual. Regresión: `[npc_ai_context]` 3/130,
percepción+escena+self 19/167 y `[npc_ai]` 128/1669, todo con semilla 1.

### Fase 4 final — Social Director

Comando: `npc_ai_baseline_requests_per_shared_event --rng-seed 1 -d yes`.

| observadores | requests LLM | profundidad de cola | total_us |
|---:|---:|---:|---:|
| 1 | 1 | 1 | 30 010 |
| 5 | 1 | 1 | 21 158 |
| 10 | 1 | 1 | 41 290 |
| 20 | **1** | **1** | **76 499** |

Frente al BASELINE CLEAN, el evento físico compartido con 20 observadores baja
de 20 a 1 request (−95 %). El tiempo es diagnóstico y varía con el entorno; el
gate arquitectónico es la cantidad de requests y la profundidad de cola.

El test no oculto
`combat_social_director_limits_a_shared_event_to_one_request_at_20_observers`
exige exactamente una request con 20 NPC. La revalidación focal de supersede
urgente, fin de combate, agarres, diálogo grupal y órdenes deterministas pasó
8 casos / 177 aserciones. Build Release x64 correcto y `[npc_ai]` con semilla 1:
**130 casos / 1723 aserciones**, todo pasa.

### Fase 6 final — Equipment y órdenes grupales

Comando: `npc_ai_phase6_group_equipment_order_scaling --rng-seed 1 -d yes`.

| NPC | afectados | fallos | cola LLM | total_us | us/NPC |
|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 0 | 0 | 323 | 323 |
| 5 | 5 | 0 | 0 | 584 | 116 |
| 10 | 10 | 0 | 0 | 1659 | 165 |
| 20 | **20** | **0** | **0** | **3212** | **160** |

El coste crece linealmente con cada acción física y persistencia por NPC; no se
crean prompts ni requests LLM. Tests focales nuevos: 4/82. Regresión completa
de equipment, incluida mochila con contenido y volver a vestir: 26/327. Build
Release x64 correcto y `[npc_ai] --rng-seed 1`: **134 casos / 1810 aserciones**.

### Pista de linterna final — indicador de sidebar

Comando: `[npc_ai_flashlight] --rng-seed 1 -d yes`.

| métrica | valor |
|---|---:|
| consultas cacheadas | 100 000 |
| detecciones correctas | 100 000 |
| total | 11 837 us |
| coste por consulta | **118 ns** |

El benchmark precalienta el caché con una `flashlight_on` transportada. La ruta
consulta `item::is_emissive`, no los flags `LIGHT_n` descartados al finalizar
los tipos. Foco de mensajes + widget + benchmark: 3 casos / 66 aserciones.
Build Release x64 correcto y `[npc_ai] --rng-seed 1`: **136 casos / 1872
aserciones**, todo pasa.

### Diagnóstico Qwen/Ollama — instrumentación inicial de latencia

En el bloque inicial no se hizo una llamada real a Ollama. Esa limitación ya no
aplica al A/B B+D documentado debajo; este párrafo conserva la validación
sintética de la instrumentación:

| métrica de validación | valor |
|---|---:|
| requests sintéticas agregadas | 1 |
| éxitos / errores | 1 / 0 |
| bytes del prompt asíncrono | 23 |
| casos / aserciones `[npc_ai_ollama]` | 2 / 34 |
| suite `[npc_ai]` con semilla 1 | **138 / 1912** |

La salida incluye promedio y máximo por sesión de: bytes de system, prompt y respuesta,
profundidad de cola, preparación, espera en cola, arranque del worker, HTTP,
parseo JSON, espera de completion, recogida del hilo principal, validación y
tiempo total. El coste y el I/O son cero en gameplay normal salvo la consulta
barata del gate: con `CDDA_NPC_AI_DEBUG` desactivado el test confirma que no se
crea `npc_ai_ollama_diagnostics.txt`.

### Corrección Qwen/Ollama B+D — bytes e idioma

Este A/B histórico se midió con `num_predict=96`. El payload vigente después
de Combat Social conserva `temperature=0.4`, `top_p=0.85`, `top_k=20`,
`repeat_penalty=1.1` y los stops, y añade `num_ctx=16384`, `seed=1` y
`num_predict=192` para cerrar el JSON batch. Modelo **`qwen3:14b` sin cambios**.

Comando de bytes:
`npc_ai_baseline_prompt_cost_by_query_type --rng-seed 1 -d yes`.

| consulta | antes prompt/system/total B | después prompt/system/total B |
|---|---:|---:|
| saludo | 2963 / 0 / 2963 | **312 / 1315 / 1627** |
| estado propio | 3800 / 0 / 3800 | **1149 / 1315 / 2464** |
| escena breve | 6201 / 0 / 6201 | **3550 / 1315 / 4865** |
| escena detallada | 13 240 / 0 / 13 240 | **10 589 / 1315 / 11 904** |
| memoria | 3069 / 0 / 3069 | **418 / 1315 / 1733** |
| orden/general | 2972 / 0 / 2972 | **321 / 1315 / 1636** |

Grupo de 20 NPC: **22 638 B prompt + 26 321 B system = 48 959 B**, frente a
75 679 B en el cierre de Fase 3. La reducción total es 35,3 % y sigue siendo
lineal; el límite de contexto cuenta ahora ambos campos.

Comando live opt-in:
`npc_ai_ollama_live_language_retry_rate --rng-seed 1 -d yes`. Corpus fijo de
12 turnos en `es_ES` (6 normales + 6 con intento de forzar inglés):

| versión | prompt total/promedio B | system total/promedio B | reintentos | tasa | fallos finales |
|---|---:|---:|---:|---:|---:|
| antes | 41 175 / 3431 | 0 / 0 | 4/12 | **33,33 %** | 0 |
| después, primaria | 9363 / 780 | 17 088 / 1424 | 3/12 | **25,00 %** | 0 |
| después, repetición | 9363 / 780 | 17 088 / 1424 | 3/12 | **25,00 %** | 0 |

El total del corpus baja 35,8 % y los reintentos 25 % relativos. La semilla fija
CDDA/NPC/contexto y bytes, no el RNG interno de Ollama; por eso se conservó una
repetición posterior. Detalle completo y la corrida intermedia descartada en
`npc_ai_metrics.md`, Q4.

### Sobre la aparente duplicación de bytes

Entre dos ejecuciones sin semilla los bytes pasaron de 11 678 a 27 377 para el mismo saludo, sin cambios de código. **No era una regresión.** Se descartó estado acumulado en disco (`test_user_dir` no contiene archivos de memoria de NPC). La causa es la generación aleatoria del NPC: distinta semilla produce distinto inventario, rasgos y biografía. **Demostración:** con `--rng-seed 1` los bytes son idénticos en corridas consecutivas (9847 / 9765 / 9767 / 16475 / 16491 / 9865). Ver sección 22.

### Combat Social A+D+B+V final — traza fija y Ollama real

La traza `five_facts_four_seconds` reproduce los mismos cinco hechos durante
una ventana de burst de 16 s. No representa una tasa sostenida: mide batching,
filtros y scheduler con la misma entrada. Configuración final: `qwen3:14b`,
`num_ctx=16384`, `num_predict=192`, `seed=1`; 0/27 prompts reales truncados.

| métrica | RAW | NEW |
|---|---:|---:|
| líneas útiles del grupo/min | 18,75 | 15,00 |
| inferencias/min | 18,75 | **3,75** |
| líneas útiles/inferencia | 1,00 | **4,00** |
| eventos narrables capturados/verbalizados | 5/4 | 5/4 |
| cooldown/dedup/expiry/conocimiento | 16/0/0/0 | **0/0/0/0** |
| fallback; cola máxima/p95 | 0; 2/2 | **0; 1/1** |

NEW produce el burst natural exigido de cuatro líneas con una sola inferencia,
una línea de grupo por turno y distribución 2/1/1 entre tres speakers. RAW
produce cinco líneas pero necesita cinco inferencias y descarta 16 candidatas
por cooldown. El volumen instantáneo no se maximiza a costa de legibilidad;
la mejora jugable sostenida viene de bajar el gap normal/urgente de 30/5 s a
8/2 s y de poder servir varias líneas por lote. Protocolo y detalle completo:
sección 14 y `npc_ai_metrics.md`, S3-S6.

---

## 14. COMANDOS EXACTOS

Todos verificados en esta máquina. Shell: **PowerShell**. Usar `;` como separador, **no** `&&`.

### Compilación incremental (target de tests)

```powershell
cd C:\CDDA-AI\Cataclysm-DDA\msvc-full-features
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "Cataclysm-vcpkg-static.sln" /t:"Cataclysm-test-vcpkg-static" /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
```

Salida: `C:\CDDA-AI\Cataclysm-DDA\Cataclysm-test-vcpkg-static-Release-x64.exe`.
Duración típica: 15-70 s incremental. Aviso benigno esperado: `LNK4315 /DEBUG:FASTLINK ya no se admite`.

Localizar MSBuild si cambia la instalación:
```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe"
```

**No hay que editar el `.vcxproj` para añadir tests**: se recogen con el comodín `..\tests\*.cpp`.

### Suite de regresión

```powershell
cd C:\CDDA-AI\Cataclysm-DDA
.\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai]" --rng-seed 1
.\Cataclysm-test-vcpkg-static-Release-x64.exe --order decl --rng-seed 1
```

El primer comando es el gate rápido de NPC AI. El segundo, sin filtro, es
obligatorio antes de cerrar una sesión o iniciar otra fase; no se debe inferir
el estado global del proyecto a partir de `[npc_ai]`.

### Benchmark — BASELINE CLEAN

```powershell
cd C:\CDDA-AI\Cataclysm-DDA
.\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai_baseline]" --rng-seed 1 -d yes
```

### Benchmark — RAW-equivalente (logging activado)

```powershell
cd C:\CDDA-AI\Cataclysm-DDA
$env:CDDA_NPC_AI_DEBUG="1"
.\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai_baseline]" --rng-seed 1 -d yes
Remove-Item Env:\CDDA_NPC_AI_DEBUG
```

### Probe de rendimiento preexistente

```powershell
.\Cataclysm-test-vcpkg-static-Release-x64.exe "npc_ai_performance_probe" -d yes
```

### Casos concretos del banco

```powershell
.\Cataclysm-test-vcpkg-static-Release-x64.exe "npc_ai_baseline_perception_cost_by_radius,npc_ai_baseline_prompt_cost_by_query_type" --rng-seed 1
.\Cataclysm-test-vcpkg-static-Release-x64.exe "npc_ai_baseline_hot_path_scaling" --rng-seed 1
.\Cataclysm-test-vcpkg-static-Release-x64.exe "npc_ai_baseline_requests_per_shared_event" --rng-seed 1
.\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai_hygiene]" --rng-seed 1
```

### Variables de entorno de diagnóstico

| Variable | Efecto | Definida en |
|---|---|---|
| `CDDA_NPC_AI_DEBUG=1` | Activa los writers de diagnóstico | `src/npc_ai_debug.cpp` |
| `CDDA_NPC_AI_PROFILE=1` | Activa el profiler por subsistema | `src/npc_ai_profiler.cpp` |
| `CDDA_NPC_AI_EVENT_DEBUG=1` | Activa el JSONL del event stream | `src/npc_ai_event_stream.cpp` |

### Paso 0 + Combat Social A/B final (29/08/2026)

Ollama real: `qwen3:14b` Q4_K_M, digest
`bdbd181c33f2ed1b31c972991882db3cf4d192569092138a7d29e973cd9debe8`,
`num_ctx=16384`, `num_predict=192`, `seed=1`. La medición RAW de capacidad
(`num_predict=96`) dio cold 4919 ms, warm media 382,42 ms/p95 462 ms (n=12) y
burst concurrente n=8 media 1429,88 ms/p95 2435 ms; profundidad de admisión
máxima/p95 8. Cambiar a 192 evitó cortar el JSON de cuatro candidatas; las
respuestas que paran antes no pagan necesariamente los 192 tokens.

Prompts reales: peor caso medido 3413 tokens; grupo 20 máximo 689 por petición;
0/27 truncados. `/api/ps` confirmó CONTEXT 16384 y 11 827 402 505 B de VRAM.
La guarda dura final es 15 680 bytes de input (`16384-192-512`, cota deliberada
de un byte por token). El Context Router existente es quien recorta primero;
batch reduce hechos y después slots, sin crear otro router.

Traza fija: `tests/data/npc_ai_combat_social_trace.json`, 5 hechos en 4 s; se
mide una ventana de burst de 16 s. Las tasas no son objetivo sostenido:

| métrica | RAW | NEW |
|---|---:|---:|
| líneas útiles del grupo/min | 18,75 | 15,00 |
| inferencias/min | 18,75 | 3,75 |
| líneas útiles/inferencia | 1,00 | **4,00** |
| eventos narrables capturados/verbalizados | 5/4 | 5/4 |
| descartes cooldown/dedup/expiry/conocimiento | 16/0/0/0 | 0/0/0/0 |
| descartes validación/promesas | 0/0 | 0/0 |
| fallback | 0 | 0 |
| cola máxima/p95 | 2/2 | **1/1** |

Fairness por speaker (líneas/min en la ventana): RAW 7,5 / 7,5 / 3,75; NEW
7,5 / 3,75 / 3,75 (líneas 2/1/1). NEW cumple el burst de 3-4 líneas con una
sola inferencia, mantiene una única línea de grupo por turno y reduce presión de
cola. No maximiza artificialmente líneas/min: preserva legibilidad y espera
hechos narrables. El filtro de promesas descartó 0 en esta traza; la métrica
queda instrumentada para gameplay futuro sin cambiar todavía su conducta.

### Notas de PowerShell

- `&&` produce `ParserError: InvalidEndOfLine`. Usar `;`.
- `$b..HEAD` se interpreta como acceso a propiedad. Usar `${b}..HEAD`.
- `sed`, `head`, `cat` no están disponibles.
- El binario de tests escribe a stderr, lo que PowerShell muestra como `NativeCommandError`. **No es un fallo**, es ruido de la consola.

---

## 15. TESTS

### Suite de regresión actual

```
[npc_ai] --rng-seed 1 → 157 casos, 2203 aserciones, TODO PASA
```

El cierre de conversación añade routing DIRECT/GROUP con origen durable,
aislamiento de cadenas antiguas NPC->NPC, categorías contextuales, prompt final
con salud real, contextos distintos por NPC, percepción/inventario reales,
dedupe cruzado y fallback español de equipo. Baseline inmediato antes de este
bloque: 147 casos / 2027 aserciones; resultado: +10 casos / +176 aserciones,
sin fallos.

### Suite completa sin filtro

Comando obligatorio:

```powershell
.\Cataclysm-test-vcpkg-static-Release-x64.exe --order decl --rng-seed 1
```

Último resultado global limpio anterior, 29/08/2026; no se repitió durante el
bloque corto de conversación porque el protocolo vigente pidió focos más gate
`[npc_ai]`:
**1291 casos; 1279 pasan / 12 fallan; 19 aserciones fallan; 1299,04 s; exit code
1 solo por esos fallos de aserción, sin crash**. El inventario `--list-tests`
confirma el denominador de 1291 y el log compacto queda en
`test_user_dir/npc_ai_full_gate_clean.log`.

Comparación nominal: las ubicaciones fallidas de la corrida completa se
mapearon a sus `TEST_CASE` actuales y forman exactamente el mismo conjunto de
12 nombres congelado antes de tocar código funcional. No basta una igualdad de
contadores: **no apareció ningún nombre nuevo ni desapareció uno antiguo**.

Los 12 casos globales restantes son ajenos a NPC AI y ya fallaban antes de este
arreglo:

- `tname_i18n_order`.
- `Glass_portion_breakability`.
- `mission_goal_condition_test`.
- `limit_mod_size_bonus`, `monsters_spawn_eggs`,
  `monsters_spawn_egg_itemgroups`, `monsters_spawn_babies` y
  `monsters_spawn_baby_groups`.
- `TranslationDocument_loads_valid_MO`,
  `TranslationManager_translates_message`,
  `TranslationManager_translates_message_with_context` y
  `TranslationManager_translates_plural_messages`; falta el artefacto generado
  `data/mods/TEST_DATA/lang/mo/ru/LC_MESSAGES/TEST_DATA.mo`.

Una primera pasada de esta sesión informó 13 casos / 22 aserciones por
`achievements_tracker`: `test_user_dir/achievements` conservaba un archivo
`Alpha Avatar` de una ejecución anterior. El caso pasó aislado (1/59) al mover
solo los archivos exactos a `test_user_dir/gate_artifact_backup_20260829`; la
suite limpia posterior produjo el resultado nominal de arriba. La operación es
reversible y el archivo que la suite volvió a generar al salir se retiró también
del directorio activo para no contaminar la próxima corrida.

Los seis fallos de mission/monster son dependientes del orden/estado previo y
pueden pasar al filtrarlos aisladamente; por eso el gate autoritativo es la
corrida completa con `--order decl`, no una suma de filtros individuales.

Foco: `[npc_ai_ollama] --rng-seed 1` = **2 casos / 34 aserciones**. Prueba
modelo y payload exactos, seis opciones explícitas, campo system, parser válido
e inválido, ausencia de archivo con gate off y contenido system/prompt/raw/
limpio más resumen agregado con gate on. No invoca Ollama ni necesita partida.

Benchmark oculto opt-in:
`npc_ai_ollama_live_language_retry_rate --rng-seed 1 -d yes`. Sí invoca el
`qwen3:14b` local. RAW dio 3/12 reintentos (25 %); después de fijar
`num_ctx=16384`, seed y telemetría, dos corridas dieron 4/12 (33,33 %), cero
fallos finales y cero truncamientos. No forma parte de la suite normal para que
`[npc_ai]` no dependa de un servicio externo.

### Los 25 tests obligatorios

> **AVISO DE FIDELIDAD:** la lista literal de los 25 tests está en el "PROMPT MAESTRO" del usuario, en la transcripción enlazada al inicio. El agente anterior **no conserva su enunciado exacto**. La tabla siguiente marca con certeza los 3 implementados y **reconstruye** el resto a partir de los requisitos de cada fase. **Codex debe recuperar la lista literal antes de dar la fase de tests por cerrada.**

| # | Test | Estado |
|---|---|---|
| 1 | Logging de diagnóstico silencioso salvo activación explícita | **[PASS]** `npc_ai_diagnostic_logging_is_silent_unless_explicitly_enabled` |
| 2 | El estado por NPC no sobrevive a un cambio de sesión | **[PASS]** `npc_ai_per_npc_state_does_not_survive_a_session_change` |
| 3 | Banco de medición reproducible 1/5/10/20 NPC | **[PASS]** 5 casos en `npc_ai_baseline_bench.cpp` |
| 4 | 20 NPC activos: coste por NPC no crece más de 1,5× | **[PASS]** `combat_social_snapshot_work_is_bounded_per_observer_at_20_npcs`; benchmark 1,375× |
| 5 | Prompt grupal con 20 NPC dentro de presupuesto | **[PASS]** `npc_ai_group_self_state_prompts_stay_within_budget_at_20_npcs` |
| 6 | Self State: "¿cómo estás?" no escanea la escena | **[PASS]** `npc_ai_current_self_query_does_not_scan_the_scene` |
| 7 | Self State refleja HP/heridas/sangrado reales de vanilla | **[PASS]** `npc_ai_self_snapshot_uses_vanilla_hp_wounds_and_bleeding` |
| 8 | Stamina solo lectura, mecánica intacta | **[PASS]** `npc_ai_self_snapshot_reads_stamina_without_changing_it` |
| 9 | Context Router: cada intención incluye solo su contexto | **[PASS]** `npc_ai_context_router_includes_only_intent_relevant_context` |
| 10 | Context Router: presupuesto de prompt respetado | **[PASS]** `npc_ai_context_router_respects_the_prompt_budget_for_each_intent` |
| 11 | Social Director: 20 observadores de un evento ⇒ ≤1 request | **[PASS]** `combat_social_director_limits_a_shared_event_to_one_request_at_20_observers`; exige 1 request y cola 1 |
| 12 | Social Director: evento importante permite hasta 2 | **[PASS]** `social_director_allows_two_important_reactions_and_one_normal_reaction` |
| 13 | "Talk to everyone" conserva respuesta individual por NPC | **[PASS]** test de `enqueue_group_ai_dialogue`: contexto en prompt y personalidad distinta de Liam/Kim en system; pasa con un sonido global obsoleto inyectado antes del fixture |
| 14 | Orden determinista grupal no genera peticiones LLM | **[PASS]** `group_ai_tactical_orders_change_all_and_only_eligible_allies` también verifica cola intacta |
| 15 | Fairness de cola entre NPC | **[PASS preexistente]** `ai_scheduler_prioritizes_player_but_services_old_ambient_work` |
| 16 | Event bus: un evento se procesa una sola vez tras recargar | [NO IMPLEMENTADO] — Fase 5 |
| 17 | Event bus: sin listeners duplicados al cambiar de mundo | [NO IMPLEMENTADO] — Fase 5 |
| 18 | Event bus: sin referencias a NPC destruidos | [NO IMPLEMENTADO] — Fase 5 |
| 19 | Arma perdida por desarme en melee se recuerda | **[PASS]** `npc_ai_melee_disarm_remembers_the_npcs_lost_weapon` |
| 20 | Arma perdida por bio-op se recuerda | **[PASS]** `npc_ai_bio_op_disarm_remembers_the_npcs_lost_weapon` |
| 21 | Mochila soltada se recupera y se vuelve a vestir | **[PASS]** `npc_ai_equipment_explicitly_recovers_and_wears_same_ground_backpack` |
| 22 | Orden grupal: cada NPC identifica SU propio objeto | **[PASS]** `npc_ai_group_equipment_orders_resolve_each_npcs_own_weapon`; cola LLM 0 |
| 23 | SQLite: WAL activo y sin corrupción al cambiar de mundo | [NO IMPLEMENTADO] — Fase 7 |
| 24 | SQLite: compatibilidad con DB existente | [NO IMPLEMENTADO] — Fase 7 |
| 25 | Linterna: mensaje correcto e indicador de sidebar | **[PASS]** `tool_transform_when_activated` valida ambos sentidos y `active_light_sidebar_indicator_tracks_carried_light_sources` valida el widget |

---

## 16. INVARIANTES / FUNCIONES QUE NO DEBEN ROMPERSE

Ninguno de estos subsistemas puede degradarse. Varios tienen tests; los que no, requieren verificación manual antes de cerrar cada fase.

- **`q` / AI_TALK** — atajo de conversación con NPC
- **Conversación individual** con NPC
- **"Talk to everyone" / conversación grupal** — cada NPC responde individualmente (Aclaración 4)
- **FOLLOW** y **GUARD**
- **Sincronización WALK/RUN de seguidores** — `src/avatar.cpp` ~1302-1307
- **Stamina actual del NPC** — solo lectura, mecánica intacta
- **NPC climbing** — `src/npc_ai_climbing.cpp`, `allow_vertical_climbing` en `src/pathfinding.h` ~151, expansión de z-level en `src/pathfinding.cpp` ~547-710. Tests `[npc_ai_climbing]`
- **Descenso por tejado/canalón** — sin teletransporte. Test `npc_follower_climbs_down_a_gutter_without_teleporting`
- **Recuperación de mochila** — `process_equipment_recovery()`
- **AI async** — el worker **nunca** toca `npc`, `Character` vivo, `map`, `game` ni UI. Join en shutdown (`src/game.cpp` ~482)
- **Memoria del NPC** — SQLite por mundo + archivos legacy. Ver D1
- **Personalidad** — `who.personality` en `system`; no duplicarla en el prompt de usuario
- **Percepción y visión** — un enemigo tras pared opaca no debe aparecer nunca
- **Habla espontánea en combate** — `process_combat_social()`, cooldowns **por NPC**
- **Habla NPC-a-NPC** — `maybe_enqueue_npc_reply()`, profundidad máxima de respuesta 1
- **wield / drop / pickup** — `npc_ai_wield.cpp`, `npc_ai_pickup.cpp`, `npc_ai_batch_pickup.cpp`, `npc::ai_request_pickup()`
- **Tactical AI** — `src/npc_ai_tactical.cpp`
- **Watchlist** — `check_item_watchlist()`, persistencia en disco
- **Validación de idioma** — `generated_text_matches_dialogue_language()`, reintento por idioma
- **Filtro de promesas tácticas no confirmadas** — `combat_social_text_has_unconfirmed_tactical_promise()`
- **Coordinación NPC-NPC** — `delegate_to_helper()`, `assignment_for()`
- **Vehicle unload**, **fire/stove**, **survival warmth**, **goals**

---

## 17. PROHIBICIONES GIT

**HEAD NO REPRESENTA NECESARIAMENTE EL ESTADO REAL DEL PROYECTO. EL WORKING TREE ES LA FUENTE DE VERDAD.**

Prohibido, sin excepción:

- `git reset` (en cualquier forma, especialmente `--hard`)
- `git checkout -- .` o checkout de archivos
- `git restore`
- `git clean -fd`
- `git revert` destructivo
- Reemplazar archivos completos por su versión vanilla
- `git commit`
- `git push`
- Borrado masivo de archivos
- Modificar o destruir commits existentes

Permitido y recomendado: `git status`, `git diff`, `git log`, `git branch`, `git show`.

Antes de modificar cualquier archivo: inspeccionar `git status` y `git diff`. No descartar cambios no relacionados.

**`npc_ai_spontaneous_runtime.txt`** aparece modificado (+296 líneas). Ya estaba así antes de la sesión anterior. Es un log. No lo toques.

---

## 18. RIESGOS Y REGRESIONES CONCRETOS

| # | Riesgo | Dónde | Detalle |
|---|---|---|---|
| R1 | Guarda de z-level eliminada | `npc_ai_perception.cpp` | Se confía en que `points_in_radius` recorta a límites del mapa. Si aparece acceso fuera de rango, restaurar `z < -OVERMAP_DEPTH \|\| z > OVERMAP_HEIGHT` |
| R2 | Casillas notables lejanas perdidas | `npc_ai_perception.cpp` | El tope de radio 20 en escena detallada omite terreno notable a >20. Criaturas no afectadas |
| R3 | Preguntas en presente sin percepción | `npc_ai_context.cpp` | `"ahora"` ya no dispara percepción solo. Alguna pregunta legítima puede perder contexto. Se resuelve en Fase 3 |
| R4 | Módulo nuevo con estado estático | Cualquier `npc_ai_*` | Si no se registra en `reset_all_ai_session_state()`, reaparece el bug de contaminación entre partidas |
| R5 | Writer de diagnóstico nuevo sin gate | Cualquier `npc_ai_*` | Vuelve el coste de I/O en el hot path |
| R6 | Confundir persistencia con diagnóstico | watchlist / memory / world_memory | Silenciarlos **borra memoria de NPC**. Ver D1 |
| R7 | Fan-in aplicado a "Talk to everyone" | Fase 4 | Rompería la conversación grupal. Aclaración 4 |
| R8 | Caché de percepción compartida entre NPC | Fase 1/3 | Produciría información visual **incorrecta**. Aclaración 3 |
| R9 | Listeners de event_bus duplicados | Fase 5 | Duplicación social extremadamente difícil de diagnosticar. Aclaración 5 |
| R10 | Worker de Ollama tocando estado del mundo | Fase 4/5 | Corrupción y crashes. Contrato en `npc_ai_async.h` línea 130 |
| R11 | Pragmas SQLite sin lifecycle coherente | Fase 7 | Riesgo de corrupción. Por eso se movieron a Fase 7. Aclaración 2 |
| R12 | Romper compatibilidad con DB existente | Fase 7 | Hay saves reales con datos. Ver backups de migraciones previas |
| R13 | Medir sin semilla fija | Cualquiera | Conclusiones falsas de hasta 2,4×. Ver D7 y sección 22 |
| R14 | Optimizar el hot path silenciando NPC | Fase 4 | El objetivo no es silenciar al grupo. Aclaración 6 |
| R15 | Modificar la mecánica de stamina | Fase 2 | Prohibido explícitamente. Solo lectura |

---

## 19. QWEN / OLLAMA

### Estado final del bloque B+D

- Cliente WinHTTP en `src/npc_ai_client.cpp`: `POST
  http://localhost:11434/api/generate`, `Content-Type: application/json;
  charset=utf-8`; timeouts resolve/connect/send/receive =
  3000/3000/5000/60000 ms.
- Un worker de transporte y seis carriles de prioridad en
  `src/npc_ai_async.cpp`; el estado del mundo solo se valida y modifica al
  ejecutar `process_ai_completions()` en el hilo principal.
- El modelo continúa siendo **`qwen3:14b`**. No se creó ni descargó otro.
- Payload final, antes del escape JSON de `system` y `prompt`:

```json
{
  "model": "qwen3:14b",
  "system": "<reglas estáticas + contrato de ruta + personalidad>",
  "prompt": "<estado/contexto/evento/interacción actuales>",
  "stream": false,
  "think": false,
  "keep_alive": "30m",
  "options": {
    "temperature": 0.4,
    "top_p": 0.85,
    "top_k": 20,
    "repeat_penalty": 1.1,
    "num_ctx": 16384,
    "num_predict": 192,
    "seed": 1,
    "stop": ["<|im_start|>", "<|im_end|>"]
  }
}
```

### Por qué se eligió cada opción

| opción | motivo |
|---|---|
| `temperature=0.4` | menos deriva y mezcla de idioma, conservando variación natural |
| `top_p=0.85` | elimina cola de baja probabilidad frente al 0.95 del Modelfile local |
| `top_k=20` | conjunto moderado y estable; coincide con el modelo local pero ya no depende de él |
| `repeat_penalty=1.1` | frena bucles/repetición sin dañar nombres ni claves estructuradas |
| `num_ctx=16384` | cubre el peor prompt medido más salida y margen sin pagar la ventana máxima 40960 |
| `num_predict=192` | 96 cortaba el JSON de cuatro candidatas; 192 lo cierra sin obligar al modelo a usar toda la reserva |
| `seed=1` | hace reproducible el A/B LLM junto con la semilla CDDA y los parámetros fijos |
| stops Qwen | son los dos límites nativos mostrados por `ollama show qwen3:14b --modelfile`; no cortan `DECISION/TEXT` multilínea |

### Separación `system` / `prompt`

`build_npc_system_prompt()` centraliza los contratos por
`npc_prompt_purpose`: diálogo directo, habla espontánea, respuesta NPC-a-NPC,
Combat Social y resoluciones watch/pickup/wield. Para las cuatro rutas de habla
incluye identidad humana, reglas de grounding/acciones, formato de salida,
personalidad CDDA e idioma. Los resolutores reciben solo su contrato semántico,
sin personalidad irrelevante.

`build_npc_prompt()` conserva el estado real, relación, contexto seleccionado
por Fase 3 y frase actual. Espontánea y Combat Social agregan únicamente sus
snapshots/eventos actuales; los resolutores agregan orden y candidatos reales.
Se eliminaron de estos prompts los bloques repetidos de instrucciones y
personalidad. El presupuesto duro descuenta el tamaño del system, por lo que
siempre limita `system + prompt`, no solo la mitad visible.

`ai_request_snapshot` captura ambas cadenas en el hilo principal. El worker
recibe copias inmutables y llama al transporte con ambas; nunca consulta al NPC
ni al mundo. Los executors de test de un argumento siguen disponibles mediante
un adaptador, pero producción usa siempre la firma de dos argumentos.

### Cumplimiento de idioma y reintento

- La regla estática vive en `system`: el contenido/idioma de la frase del
  jugador nunca cambia el idioma de UI.
- El dato dinámico `OUTPUT_LANGUAGE=<idioma activo>` queda al final de cada
  prompt de habla. No duplica el bloque de instrucciones.
- `generated_text_matches_dialogue_language()` conserva nombres de entidades
  sin traducir, pero ahora también rechaza respuestas inglesas cortas antes
  aceptadas, por ejemplo `Okay.` y `Sure, sounds good.`.
- El diálogo directo conserva un único reintento dentro del mismo work item
  FIFO, para que un turno posterior no adelante la corrección. Espontánea y
  Combat Social conservan su reencolado único.
- Solo al reintentar se añade una corrección dinámica tanto al system como al
  final del prompt. Un intento intermedio de corregir solo en system dejó que
  órdenes contradictorias al final del texto ganaran prioridad; está medido en
  `npc_ai_metrics.md`, Q4.

### Parseo real de la respuesta

1. WinHTTP concatena el body completo sin streaming.
2. `parse_ollama_response_json()` exige un objeto JSON válido mediante
   `TextJsonObject`, extrae únicamente el string `response` y permite los demás
   miembros de Ollama. JSON inválido o ausencia/tipo incorrecto de `response`
   produce error, no una frase inventada.
3. La traza `RESPONSE_RAW` es el body JSON; `RESPONSE_CLEAN` es exactamente el
   campo `response` ya desescapado. En este punto no se hace trim ni se quitan
   tokens especiales.
4. Después, cada ruta aplica su parser: espontánea y Combat Social extraen
   `DECISION`/`TEXT`, normalizan una línea, quitan comillas y limitan longitud;
   watch/pickup/wield validan selectores y usan fallback seguro; diálogo directo
   quita marcadores watch, valida idioma, reintenta una vez y filtra promesas
   tácticas no confirmadas. Estos parsers no son idénticos y algunos fallbacks
   aceptan texto libre.

### Diagnóstico gateado implementado

Con `CDDA_NPC_AI_DEBUG=1`, cada llamada real añade a
`<user_dir>/npc_ai_ollama_diagnostics.txt`:

- `PARAMETERS` y tamaño + contenido separados de `SYSTEM_FINAL` y
  `PROMPT_FINAL`;
- body JSON completo como `RESPONSE_RAW`;
- `response` extraída como `RESPONSE_CLEAN`, o el error de parseo;
- `NPC_AI_LATENCY_REQUEST` con correlación, tipo/carril, bytes de system/prompt,
  profundidad de cola y tiempos por etapa;
- `NPC_AI_LATENCY_SUMMARY` acumulado desde el último cambio de sesión, con
  muestras/éxitos/errores, requests elegibles, reintentos, tasa de reintento y
  promedio/máximo de bytes, cola, preparación, HTTP, parseo, entrega,
  validación y total.

El cliente marca `http_completed_ms` al terminar la descarga y
`parse_completed_ms` después de extraer el JSON; la cola ya no los colapsa en
un mismo timestamp. `reset_all_ai_session_state()` reinicia el agregado. Si el
gate está apagado, no se abre ni se crea el archivo. La suite normal usa un
executor falso; el benchmark live es oculto y opt-in para no introducir una
dependencia externa en `[npc_ai]`.

### Medición antes/después

Todas las cifras del ejecutable usan `--rng-seed 1`.

| consulta | antes prompt/system/total B | después prompt/system/total B |
|---|---:|---:|
| saludo | 2963 / 0 / 2963 | **312 / 1315 / 1627** |
| estado propio | 3800 / 0 / 3800 | **1149 / 1315 / 2464** |
| escena breve | 6201 / 0 / 6201 | **3550 / 1315 / 4865** |
| escena detallada | 13 240 / 0 / 13 240 | **10 589 / 1315 / 11 904** |
| memoria | 3069 / 0 / 3069 | **418 / 1315 / 1733** |

Con 20 NPC: antes 75 679 B; después **22 638 B prompt + 26 321 B system =
48 959 B** (-35,3 % total).

Benchmark real de idioma con UI `es_ES`, 12 turnos (6 normales + 6 intentos de
forzar inglés), mismo transporte/validador/reintento de producción:

| versión | prompt total/promedio B | system total/promedio B | reintentos | tasa | fallos finales |
|---|---:|---:|---:|---:|---:|
| antes B+D | 41 175 / 3431 | 0 / 0 | 4/12 | **33,33 %** | 0 |
| después B+D | 9363 / 780 | 17 088 / 1424 | 3/12 | **25,00 %** | 0 |
| repetición después | 9363 / 780 | 17 088 / 1424 | 3/12 | **25,00 %** | 0 |

La tasa baja 8,33 puntos y 25 % relativos; los bytes combinados bajan 35,8 %.
La semilla fija el mundo/NPC/contexto, no el RNG interno de Ollama, por eso se
conserva la repetición. Los tiempos HTTP no se presentan como A/B controlado.

### Estado del diagnóstico A-F

- **A, contexto:** mitigada por Fases 2/3 y reducida de nuevo por la separación.
- **B, configuración:** **corregida**; ya no depende de defaults externos.
- **C, parser:** transporte estricto intacto; parsers por ruta siguen siendo un
  riesgo secundario independiente de este bloque.
- **D, idioma:** **corregida y medida**; lock + dato dinámico + validador + un
  reintento, con mejora real y cero fallos finales en el corpus.
- **E, modelo:** sigue sin estar demostrado como causa; `qwen3:14b` no cambió.

### Validación final

- Build Release x64: PASS.
- `[npc_ai_ollama] --rng-seed 1`: **2 casos / 34 aserciones**.
- contexto + idioma: **7 casos / 177 aserciones**.
- `[npc_ai] --rng-seed 1`: **141 casos / 1970 aserciones, PASS**.
- Suite completa `--order decl --rng-seed 1`: **1279/1291 pasan**; exactamente
  los 12 fallos del baseline, sin crash. Ver sección 15.
- Live posterior a `num_ctx`: dos corridas 12/12, 4/12 reintentos, 0 fallos
  finales y 0 truncamientos. La medición concurrente de cola está en S3 de
  `npc_ai_metrics.md`; ya no queda pendiente.

---

## 20. LINTERNA — CERRADA Y VERIFICADA

### 20.1 Mensaje al encender/apagar

- Los actores de `flashlight` y `flashlight_on` cargan textos distintos desde
  `data/json/items/tool/lighting.json` y `iuse_transform::use()` emite el actor
  correspondiente antes de transformar el tipo.
- No se reprodujo una inversión en producción y no se modificó el actor vanilla.
  `tool_transform_when_activated` ahora valida, en ambos sentidos, actor cargado,
  estado `active`, nombre resultante y último mensaje exacto.
- Detalle de test: el mensaje se captura inmediatamente después de `use()`. El
  listener global de Catch2 limpia `Messages` al entrar en cada sección/`THEN`;
  leer el log dentro del `THEN` producía un falso negativo.

### 20.2 Indicador en la barra lateral

- `widget_var::active_light_text` delega en
  `display::active_light_indicator( const Character & )`.
- El widget JSON `active_light_desc` muestra `Light source: on/-` y forma parte
  de `light_moon_wind_temp_layout` en el sidebar configurable.
- La detección usa
  `cache_has_item_with( "item::is_emissive", &item::is_emissive )`: es la
  sobrecarga cacheada vanilla y no recorre el inventario en cada redibujado.
- Corrección de la pista previa: no se puede consultar `LIGHT_300` como
  `flag_id`. `Item_factory::finalize_pre()` convierte cualquier `LIGHT_n` en
  `itype::light_emission` y después elimina ese flag. El predicado cubre la
  linterna y cualquier otra fuente de luz transportada.
- Gates finales con semilla 1: foco 3/66; benchmark 100 000/100 000 detecciones,
  11 837 us totales, 118 ns/consulta; suite `[npc_ai]` 136/1872.

---

## 21. ALIADOS / NEUTRALES / HOSTILES

Objetivo: preparar la arquitectura sin duplicar sistemas. **No crear una arquitectura paralela.**

**Problema concreto identificado.** Hay comprobaciones `is_player_ally()` dispersas que actúan como puerta de entrada a los subsistemas de IA. Mientras sean literalmente "¿es aliado?", extender a neutrales y hostiles obligaría a duplicar código.

**Dirección recomendada.** Refactorizar esas puertas hacia un **predicado genérico** del tipo "¿este NPC participa en este subsistema, y con qué política?", parametrizado por la actitud/facción, en vez de un booleano de aliado. Los subsistemas (percepción, social, coordinación, equipment) quedan igual; solo cambia la puerta.

Fuentes vanilla a reutilizar: `Creature::attitude_to()`, `npc::get_attitude()`, `faction` y el estado de facción. `build_sensory_snapshot()` ya usa `who.attitude_to( other )` y guarda `attitude` y `hostile` por criatura, así que la percepción **ya es neutral respecto a la facción**.

Es trabajo de refactor, no de funcionalidad nueva. Debe hacerse **después** de las Fases 3 y 4, cuando el enrutado y el Social Director ya estén estabilizados, para no refactorizar dos veces.

---

## 22. TRABAJO DESCARTADO — NO REPETIR

### 22.1 "Los bytes de prompt se duplican entre ejecuciones"

**Hipótesis inicial:** regresión, o estado persistente acumulándose (memoria de NPC creciendo sin límite e inflando el prompt).

**Observación:** el mismo saludo pasó de 11 678 a 27 377 bytes entre dos corridas, sin cambios de código en la construcción de prompts.

**Investigación:** se descartó estado en disco — los tests usan `test_user_dir` como directorio de usuario y **no contiene** archivos de memoria de NPC (solo dos logs de diagnóstico).

**Conclusión real:** variación de RNG. Distinta semilla genera NPC con distinto inventario, rasgos y biografía, y eso cambia el tamaño del prompt.

**Prueba:** dos corridas consecutivas con `--rng-seed 1` dan bytes **idénticos**: 9847 / 9765 / 9767 / 16475 / 16491 / 9865.

**NO volver a investigar esto.** Usar siempre `--rng-seed 1`.

### 22.2 Corto-circuito de notabilidad en la percepción

**Hipótesis:** el coste por casilla está en construir la observación completa (nombres traducidos de terreno y mobiliario, `tname()` por item). Evitarlo para casillas que luego se descartan debería ahorrar mucho.

**Implementación probada:** predicado `may_be_notable_tile( here, who, position )` con consultas baratas (`has_furn`, `passable`, `field_at().field_count()`, `veh_at`, flags de puerta/ventana/escalera/rampa, `could_see_items` + `i_at`, `can_see_trap_at`), aplicado antes de construir la observación para casillas fuera del anillo cercano.

**Resultado medido:** 1 362 / 4 994 / 11 953 / 111 449 µs frente a 1 323 / 4 809 / 13 040 / 107 191 sin el cambio. **Dentro del ruido, en parte peor.**

**Conclusión:** el coste por casilla **no** está en construir nombres, sino en `who.sees( here, position )`, que se ejecuta antes. Además, en un mapa realista más casillas son notables, así que el pre-test sería un coste neto.

**Acción:** revertido. **NO reintentarlo.** Si se quiere atacar el coste por casilla, el objetivo es la prueba de línea de visión.

### 22.3 "`put_into_vehicle_or_drop` es el hook único para armas caídas"

**Hipótesis de la auditoría inicial:** engancharse ahí capturaría todos los drops involuntarios de arma.

**Verificación en código:** falso. Solo `src/character.cpp` ~8315
(lesión/caída) lo usa. El desarme recuperable de `src/melee.cpp` ~1924-1929
usa `remove_weapon()` + `here.add_item_or_charges()`, y `bio_op_disarm` en
`src/monattack.cpp` ~4370-4380 usa `i_rem()` + `here.add_item_or_charges()`.
El `remove_weapon()` de `melee.cpp` ~2336 destruye un arma de vidrio ya hecha
añicos y no representa un drop recuperable.

**Conclusión:** se implementaron tres puntos de enganche explícitos que llaman
al mismo adaptador de memoria. No enganchar `remove_weapon()` globalmente. Ver
sección 11.

### 22.4 Rama base

`origin/master` **no** es la base. La divergencia real parte de `origin/0.I-branch` (tag `0.I`). Cualquier `git diff` contra master da un resultado inflado y engañoso.

### 22.5 Archivos irrelevantes en el repositorio

El repositorio contiene un soundpack y copias de seguridad de partidas (`save-backup-20260826-220955/`) que inflan los recuentos de archivos cambiados. Están excluidos del análisis de código. No los borres, pero ignóralos.

---

## 23. RECOGIDA DIRIGIDA: CONSERVACION Y CANDIDATOS (29/08/2026)

**Cerrado en codigo y tests; pendiente de prueba jugable del jugador.**

- La ruta dirigida ya no usa `item_location::obtain()` para almacenamiento.
  `obtain()` copia mediante `Character::i_add()`, elimina despues el origen del
  mapa sin comprobar que el destino sea propiedad del personaje y solo devuelve
  una localizacion si `ch.has_item()` reconoce el resultado. Sus fallbacks
  permiten wield/drop, de modo que no ofrece la atomicidad necesaria aqui.
- La transferencia dirigida usa `try_add(..., allow_wield=false)`, marca y
  verifica la copia exacta, y elimina el origen solo despues de confirmar un
  destino `held_by()` valido. En fallo elimina la copia identificada y conserva
  el objeto original en el suelo, con su estado interno.
- Se elimino de esta ruta el `scan_new_items()` general. Ese scan examinaba todo
  el inventario y podia equipar un cuchillo/arma anterior en vez del target. La
  prevalidacion y la ejecucion llaman ahora a `validate_wield_target()` con la
  misma `item_location` resuelta y registran REQUESTED/RESOLVED/VALIDATED/OBTAINED.
- El resolver ya no corta tras 30 items fisicos. Primero agrupa solo la
  representacion logica de items apilables equivalentes por casilla, prioriza
  asociacion fiable existente, coincidencia de nombre/orden y distancia, aplica
  desempate estable y solo entonces limita a 30. Los items fisicos no se tocan.
- Reproduccion previa al fix: 2 casos, 18 aserciones, **2 fallos** (hacha fuera
  del presupuesto; cuchillo ajeno equipado). Tras el fix: 4 casos nuevos, 66
  aserciones, PASS. Relacionados: 37/475 y vanilla item_location+wield 7/1216,
  PASS. Gate `[npc_ai] --rng-seed 1`: **161 casos / 2269 aserciones, PASS**;
  baseline anterior 157/2203, incremento exacto +4/+66, cero regresiones.
- Build Release x64 del juego: **PASS**. Binario:
  `C:\CDDA-AI\Cataclysm-DDA\cataclysm-tiles.exe`.
- `git diff --check` de produccion/tests/docs: PASS. El comando sin exclusiones
  sigue señalando espacios finales ya presentes en los logs de gameplay locales
  `npc_ai_ollama_diagnostics.txt` y `npc_ai_pickup_v1_runtime.txt`; se
  preservaron como evidencia y no pertenecen a este cambio.
- No se hizo commit ni push. FOLLOW/movimiento no fue modificado. En prueba
  manual, si el NPC oscila sin llegar al objeto, mover al jugador unos pasos;
  ese bug conocido queda fuera de alcance.

---

## 24. SESION INTENCION DE ADQUISICION / COLOR / PARSER (29/08/2026)

### Mapa de intencion en dos etapas (registrado antes de modificar A)

- **Etapa 1, antes de conocer el target:** `src/npctalk.cpp`,
  `game::ai_talk()`, entrega la linea a los dispatchers deterministas;
  `src/npc_ai_pickup.cpp`, `try_handle_pickup_command()`, y
  `src/npc_ai_wield.cpp`, `try_handle_wield_command()`, reconocen hoy las
  familias pickup/wield. La integracion de A debe clasificar aqui
  `WIELD_EXPLICIT`, `STORE_EXPLICIT` o `AMBIGUOUS`, sin inferir el target por
  palabras de su nombre.
- **Etapa 2, con target real resuelto:** `src/npc_ai_pickup.cpp`,
  `apply_pickup_ai_completion()`, vuelve del indice del resolver al
  `item_location` fisico y llama `npc::ai_request_pickup()`; la decision final
  y la transferencia ocurren en `src/npcmove.cpp`, `npc::pick_up_item()`.
  Solo aqui una intencion ambigua puede convertirse en WIELD por metadata real
  de arma; botella/medicina/objeto comun conservan STORE/AUTO.
- Los campos transitorios `ai_directed_pickup_*` de `src/npc.h` no aparecen en
  `src/savegame_json.cpp` ni en los otros writers de save auditados. La nueva
  intencion debe seguir el mismo lifecycle transitorio; no se inventara una
  persistencia parcial incompatible.
- **A11 confirmado antes del cambio:** la rama de almacenamiento dirigido deja
  `has_new_items=false`, pero la rama de wield dirigido deja `true` en
  `npc::pick_up_item()`. `npc::move()` lo consume mediante
  `has_new_items && scan_new_items()`, por lo que puede reemplazar el target
  pedido por otra arma. El arreglo debe limitarse a la ruta dirigida.

### CERRADO Y VERIFICADO

- **D, parser espontaneo:** `src/npc_ai_spontaneous.cpp`,
  `parse_spontaneous_response()`/`extract_text_field()`. La causa era que
  `TEXT=` solo se aceptaba al inicio de una linea. Ahora el parser es superset:
  multilinea y compacto, `TEXT` terminal con comas literales, case/whitespace,
  prioridad `SILENT` y diagnosticos `NO_TEXT_FIELD`, `MODEL_EMPTY` y
  `SANITIZED_TO_EMPTY`. Test: 3 casos / 26 aserciones, PASS.
- **C, color/representacion:** `src/npc_ai_pickup.cpp` y boundaries AI de
  contexto/percepcion/equipo. `remove_color_tags()` limpia nombres enviados a
  prompts, habla y logs; la clave logica usa posicion+`type_id`+nombre limpio y
  `display_stacked_with()` conserva diferencias fisicas. `damage`, `charges` y
  asociacion siguen como metadata estructurada; ningun `item_location` fisico
  se fusiona o elimina. `[npc_ai_pickup]`: 15/338, PASS.
- **A, intencion de adquisicion:** `src/npc_ai_pickup.h/.cpp`,
  `classify_acquisition_intent()` (etapa 1) y
  `apply_pickup_ai_completion()` (etapa 2); `src/npcmove.cpp`,
  `npc::ai_request_pickup()`/`npc::pick_up_item()` ejecutan un unico pipeline.
  `WIELD`, `STORE` y `AUTO` llegan hasta el target real; `toma/take` solo se
  resuelven como wield tras comprobar metadata real de arma. Se soportan las
  formas plurales requeridas y cada NPC grupal resuelve su propio target.
  Mochila conserva prioridad recover-and-wear y `Recoge tu mochila.` termina
  `WORN`. La transferencia atomica previa no se reescribio.
- **Conclusion A11:** el riesgo era real. Un wield dirigido dejaba
  `has_new_items=true` y el siguiente `scan_new_items()` podia reemplazar el
  target. Solo el cierre de adquisicion dirigida ahora lo deja `false`; el
  reequipado vanilla externo no cambia. Test con un hacha objetivo danada y
  otra hacha transportada de mayor valor confirma que queda empunada la
  instancia objetivo exacta. `[npc_ai_acquisition]`: 3/176, PASS.
- **B, ownership comando/dialogo:** `src/npctalk.cpp`, `game::ai_talk()`, y
  `src/npc_ai_pickup.cpp`, `execute_group_acquisition_command()`. Un comando
  reconocido retorna antes de `DIRECT_PLAYER_DIALOGUE` o
  `GROUP_PLAYER_DIALOGUE`; `PENDING` y `REJECTED` siguen perteneciendo al
  executor. Debug registra `COMMAND_INTERCEPTED`, `COMMAND_STATE` y
  `DIALOGUE_SUPPRESSED=yes`. Las preguntas requeridas no se interceptan y las
  requests observadas son solo `pickup_resolution`; espontaneo/NPC-NPC no se
  modifico. `[npc_ai_command_ownership]`: 3/107, PASS.
- Save/load: los campos `ai_directed_pickup_*` siguen transitorios y no se
  serializan; auditado de nuevo contra `src/savegame_json.cpp` y writers de
  save.
- Validacion final: equipment regression 17/200 PASS; gate
  `[npc_ai] --rng-seed 1` **167 casos / 2503 aserciones / PASS**, contra
  161/2269 de entrada (+6/+234, cero regresiones); `git diff --check` acotado
  PASS; `cataclysm-tiles.exe` Release x64 PASS.
- **PENDIENTE deliberado:** FOLLOW queda fuera de alcance. Evidencia preservada:
  se calculo `ENGINE_MOVE_NEXT=(69,67,-1)`, pero el NPC aparecio despues en
  `(70,68,-1)` con distancia cercana a 3 durante varios turnos; se reprodujo
  dos veces tras recargar. No atribuirlo a adquisicion.

---

# START HERE — NEXT CODEX SESSION

**No repitas la auditoría completa. Ya está hecha y sus conclusiones están en este documento.**

Orden exacto:

1. **Leer solo QUICK START** y después las secciones puntuales de la fase activa.
2. `git status` y `git diff` son la fuente de verdad. HEAD conocido:
   `adf1608531`; hay cambios locales validos posteriores. No tocar los logs de
   gameplay ni descartar trabajo previo.
3. La recogida dirigida de las secciones 23-24 esta cerrada automaticamente y
   espera prueba en partida. Gate actual: **167 casos / 2503 aserciones, PASS**.
4. No iniciar FOLLOW, movimiento, combate ni otra fase hasta recibir los nuevos
   logs y la prioridad expresa del jugador.
5. Conservar este gate de entrada; `[npc_ai]` es el tramo rápido y la suite
   completa sin filtro es obligatoria:
   ```powershell
   cd C:\CDDA-AI\Cataclysm-DDA\msvc-full-features
   & "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "Cataclysm-vcpkg-static.sln" /t:"Cataclysm-test-vcpkg-static" /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
   cd C:\CDDA-AI\Cataclysm-DDA
   .\Cataclysm-test-vcpkg-static-Release-x64.exe "[npc_ai]" --rng-seed 1
   .\Cataclysm-test-vcpkg-static-Release-x64.exe --order decl --rng-seed 1
   ```
   Esperado para `[npc_ai]`: **167 casos, 2503 aserciones, todo pasa.** La
   expectativa global y cualquier fallo conocido deben quedar registrados en
   la sección 15; un proceso truncado no cuenta como corrida completa.

No pidas autorización entre fases mientras compile, los tests pasen y los gates se cumplan. Para solo ante una decisión destructiva o una incompatibilidad arquitectónica importante.

---

# READY-TO-PASTE CODEX PROMPT (HISTÓRICO — NO USAR; VER QUICK START)

```
Vas a continuar un trabajo de ingeniería ya empezado por otro agente en el
repositorio C:\CDDA-AI\Cataclysm-DDA (Cataclysm: Dark Days Ahead con un
sistema propio de NPC AI conectado a Ollama).

DOCUMENTO DE TRASPASO: lee primero, entero, el archivo NPC_AI_HANDOFF.md que
está en la raíz del repositorio. Después lee npc_ai_metrics.md. También sigue
vigente AGENTS.md.

NO REPITAS LA AUDITORÍA. Ya está hecha. Sus conclusiones, las decisiones de
arquitectura, las mediciones base y las hipótesis ya descartadas están en
NPC_AI_HANDOFF.md. Repetirla sería desperdiciar el contexto.

EL WORKING TREE ES LA FUENTE DE VERDAD. HEAD NO representa el estado real del
proyecto: hay 29 archivos modificados y varios nuevos sin commitear que
contienen trabajo válido y verificado.

PROHIBIDO, sin excepción:
- git reset, git checkout, git restore, git clean, git revert destructivo
- reemplazar archivos completos por su versión vanilla
- git commit, git push
- descartar cualquier cambio existente

QUÉ HACER:
1. Verifica brevemente el estado: git status, git diff --stat, y compila el
   target de tests. Confirma que la suite [npc_ai] sigue verde
   (esperado: 121 casos, 1436 aserciones). Los comandos exactos de PowerShell
   están en la sección 14 del handoff.
2. Retoma exactamente donde se quedó: Fase 1 pendiente, eliminar el O(N²) de
   build_combat_perception_snapshot() en src/npc_ai_combat_social.cpp
   (~línea 991-1037). La sección 6 del handoff tiene el diagnóstico, la
   propuesta y las invariantes que no puedes romper.
3. IMPLEMENTA, no te limites a analizar. El entregable es código corregido y
   funcionando, no un informe de hallazgos.
4. Por cada fase: compila, ejecuta los tests correspondientes, mide contra el
   baseline existente con --rng-seed 1 (obligatorio, si no las mediciones no
   son comparables), comprueba que no hay regresiones y registra los números
   en npc_ai_metrics.md. Solo entonces pasa a la siguiente fase.
5. Continúa después con las Fases 2, 3, 4, 5, 6 y 7 descritas en el handoff.
6. Completa la pista independiente de la linterna (sección 20).
7. Completa la preparación para NPC aliados/neutrales/hostiles (sección 21),
   sin crear una arquitectura paralela.
8. Completa el diagnóstico y la configuración de Qwen/Ollama (sección 19).
   No cambies todavía el modelo qwen3:14b.
9. Implementa los 25 tests obligatorios (sección 15). Hay 3 hechos. La lista
   literal está en el prompt maestro del usuario; si la necesitas, pídesela.
10. Entrega el informe final con los entregables A-S (sección 1). Si necesitas
    la lista literal de A-S, pídesela al usuario.

No pares a pedir autorización entre fases mientras compile, los tests pasen y
los gates se cumplan. Para solo ante una decisión destructiva o una
incompatibilidad arquitectónica importante.

No hagas commit ni push al terminar.

Empieza por la sección "START HERE — NEXT CODEX SESSION" del handoff.
```
