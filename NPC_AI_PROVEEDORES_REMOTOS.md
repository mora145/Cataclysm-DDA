# NPC AI con proveedores remotos (rama de pruebas)

Rama: `testing/gemini-flash`, creada sobre `cdda-ai-0.I`. Añade dos backends
remotos sin quitar Ollama. El resto del sistema (Event Stream, Combat Social,
Context Router, validación, SQLite) no cambia: solo cambia quién responde al
prompt. Todo el código nuevo vive en `src/npc_ai_client.cpp`.

| Proveedor | Modelo por defecto | Cuándo usarlo |
|---|---|---|
| `ollama` (defecto) | `qwen3:14b` local | Lo de siempre. Sin red, exige el modelo de 9 GB y 32 GB de RAM |
| `openai` / `deepinfra` | `Qwen/Qwen3-14B` en DeepInfra | **Recomendado.** Mismo modelo y mismo muestreo que el local; los prompts de Miguel valen sin recalibrar |
| `gemini` | `gemini-2.5-flash` | Alternativa. Tier gratuito inservible para combate (20 peticiones); con facturación va bien |

## Estado (03/09/2026)

- Build Release x64 de juego y tests: **PASS** con Visual Studio en `E:\Visual
  Studio` (MSVC 14.38) y vcpkg en `E:\vcpkg`.
- `[npc_ai] --rng-seed 1`: **208 casos / 4208 aserciones, PASS**.
- Tests nuevos: `[npc_ai_gemini]` y `[npc_ai_openai]` (contrato de petición y
  parser, sin red) y el oculto `[.npc_ai_live]`, que llama a los proveedores
  reales con las claves del entorno. Ejecutado el 03/09/2026: DeepInfra
  respondió por el cliente C++ del juego ("Sí, tengo tres vendas en mi
  mochila. Mi brazo derecho está herido.", 75 tokens de prompt); Gemini
  devolvió un 429 del tier gratuito que el cliente parseó correctamente.
- Pruebas de latencia y cuota contra ambos endpoints: ver abajo.
- Los dos errores de `tests/player_activities_test.cpp` que aparecieron en el
  log del primer build no se repitieron en los rebuilds incrementales; el
  exe de tests enlaza y el gate pasa.

## Configuración

Todo por variables de entorno de usuario. **Las claves nunca van en el
código, en el repo, ni en `config\`.** Tras cambiarlas hay que abrir una
terminal nueva o relanzar el juego.

### DeepInfra (Qwen3-14B, el modelo de Miguel)

```powershell
[Environment]::SetEnvironmentVariable("CDDA_NPC_AI_PROVIDER", "deepinfra", "User")
[Environment]::SetEnvironmentVariable("CDDA_NPC_AI_OPENAI_API_KEY", "<clave de deepinfra.com>", "User")
```

| Variable | Defecto | Efecto |
|---|---|---|
| `CDDA_NPC_AI_OPENAI_API_KEY` | | Obligatoria. Sin ella toda petición falla en local sin tocar la red |
| `CDDA_NPC_AI_OPENAI_HOST` | `api.deepinfra.com` | Cualquier host con API compatible OpenAI |
| `CDDA_NPC_AI_OPENAI_PATH` | `/v1/openai/chat/completions` | Ruta del endpoint |
| `CDDA_NPC_AI_OPENAI_MODEL` | `Qwen/Qwen3-14B` | Id del modelo en el proveedor |
| `CDDA_NPC_AI_OPENAI_MAX_TOKENS` | `192` | Igual que `num_predict` de Ollama |
| `CDDA_NPC_AI_OPENAI_EXTRA_JSON` | | Miembros JSON crudos añadidos al cuerpo, p. ej. `"top_k":20,"repetition_penalty":1.1` |

Con el host por defecto se envían automáticamente `top_k=20` y
`repetition_penalty=1.1`, que DeepInfra acepta, para igualar el Modelfile
local. Otros hosts solo los reciben si se ponen en `EXTRA_JSON`.

El pensamiento de Qwen3 se apaga con el sufijo oficial `/no_think` en el
mensaje del usuario. El modelo devuelve igualmente un bloque `<think></think>`
vacío que el parser elimina antes de entregar el texto.

Sirve también para Hugging Face Inference Providers, Groq, Together u otros:
cambiar host, ruta y modelo. Hugging Face reenvía a DeepInfra al mismo precio.

### Gemini

```powershell
[Environment]::SetEnvironmentVariable("CDDA_NPC_AI_PROVIDER", "gemini", "User")
[Environment]::SetEnvironmentVariable("CDDA_NPC_AI_GEMINI_API_KEY", "<clave AIza... de AI Studio>", "User")
```

| Variable | Defecto |
|---|---|
| `CDDA_NPC_AI_GEMINI_MODEL` | `gemini-2.5-flash` |
| `CDDA_NPC_AI_GEMINI_MAX_TOKENS` | `256` |

Razonamiento desactivado (`thinkingBudget: 0`). Cabecera `x-goog-api-key`.

### Diagnóstico

`CDDA_NPC_AI_DEBUG=1` escribe en `npc_ai_ollama_diagnostics.txt` bloques
`=== OLLAMA | GEMINI | OPENAI REQUEST ===` con el prompt final, la respuesta
cruda y los tokens. Las claves no se escriben nunca; solo
`api_key=present|MISSING`.

## Comportamiento común de los clientes remotos

- HTTPS vía WinHTTP con el mismo helper que Ollama. Sigue siendo solo Windows.
- Respuesta cortada por longitud (`MAX_TOKENS` en Gemini, `length` en OpenAI)
  se marca como `context_truncated`, que Combat Social ya descarta.
- Ante HTTP 429 o 503 el cliente entra en backoff. Si el cuerpo trae "retry in
  N s" respeta ese tiempo más un segundo; si no, 20 s. Las peticiones en
  backoff fallan al instante sin tocar la red, así que no consumen cuota.
- Se conserva el guard de bytes de Ollama (~15,6 KB de entrada) como tope de
  coste.

## Medido el 03/09/2026 (mismo prompt de Kim, 10 preguntas)

| | DeepInfra Qwen3-14B | Gemini 2.5 Flash gratuito |
|---|---|---|
| Latencia típica | 0,7 a 1,0 s | 0,8 a 1,7 s |
| Picos | 1,7 s | 15 a 64 s antes de bloquear |
| Bloqueo | Ninguno en 15 peticiones seguidas | 429 tras 15; sostenido ~5/min; cuota `free_tier_requests` límite 20 |
| Coste medido | $0,000377 por 12 peticiones (~$0,00003 cada una) | $0 hasta el bloqueo |
| Hechos inventados | 1 leve en 15 ("Ha salvado vidas antes") | 0 en 21 |
| Estilo | Frases naturales de una o dos oraciones | Telegráfico ("Bate.") |

Estimación con los prompts reales del juego (800 a 3.400 tokens): DeepInfra
Qwen3-14B unos $0,06 por hora de juego normal y $0,23 en combate intenso.

## Escenarios en vivo y correcciones (03/09/2026)

`tests/npc_ai_live_scenarios_test.cpp` (tag oculto `[.npc_ai_live_scenarios]`)
monta 20 situaciones reales en el motor, deja que el pipeline genere los
prompts exactos, los envía al proveedor activo y escribe un informe Markdown
con prompt, respuesta cruda, lo dicho en el juego, latencia y tokens. Sirve
para cualquier proveedor y para comparar antes/después de un cambio de prompt.

Primera pasada con Qwen3-14B en DeepInfra: 11 correctos, 5 parciales, 3
incorrectos, 1 no ejercitado. Los tres incorrectos eran un NPC afirmando lo
contrario de su estado real. Diagnóstico con los prompts exactos: dos de los
tres no eran del modelo sino del Context Router, que no reconoció la pregunta
y mandó un prompt de 300 bytes sin salud ni entorno.

Correcciones aplicadas en esta rama:

| Cambio | Archivo | Motivo |
|---|---|---|
| Palabras clave nuevas para HEALTH ("necesitas ayuda", "puedes caminar", "te encuentras", "how are you"...), PERCEPTION ("zombis cerca", "hay peligro", "estamos solos"...) y CURRENT_SITUATION ("es de noche", "esta oscuro", "que hora es", "hace frio"...) | `npc_ai_context.cpp` | Liam sangrando decía "estoy bien" porque no le llegaba el bloque de salud |
| Bloque `=== ENTORNO ACTUAL ===` (noche, luz, oscuro, exterior, clima, temperatura) en la ruta CURRENT_SITUATION | `npc_ai_context.cpp` | Tres horas tras el atardecer afirmaba que era de día; la percepción no llevaba hora ni luz |
| Línea `movilidad={puede_caminar...}` en el estado propio | `npc_ai_self.cpp` | Hallazgo 1 del backlog; pierna a 0 HP y "sí, puedo caminar" |
| Reglas de severidad: una parte grave, rota, mordida, infectada o sangrando prohíbe decir "estoy bien"; los agregados no anulan una parte grave | `npc_ai_self.cpp`, `npc_ai_context.cpp` | Con `arm_r hp=10/100 grave` en el prompt, el modelo dijo "estoy bien, solo cansado" |
| Regla de grado para diálogo directo y espontáneo: sin origen, historia, lugar, sonidos ni pasado que no estén en los datos | `npc_ai_context.cpp` | "Lo encontré en un supermercado", "escuché ruidos extraños" |
| Reglas del evento espontáneo: el dolor es global sin parte del cuerpo y con intensidad proporcional | `npc_ai_spontaneous.cpp` | `dolor=10` se convirtió en "me duele mucho la cabeza" |

Segunda pasada, mismo modelo, mismos 20 escenarios: 0 incorrectos. Los tres
casos graves pasaron a correctos o parciales ("mi brazo derecho está
gravemente dañado, pero... estoy bien"; "puedo caminar, pero con dificultad,
mi pierna izquierda está rota"; "sí, es de noche, hay mucha oscuridad"). Sin
enemigos ya no inventa ruidos; el dolor espontáneo ya no tiene localización.
Gate `[npc_ai] --rng-seed 1`: 209 casos / 4228 aserciones, sin regresión.
Latencia media de DeepInfra: 8,7 s en la primera pasada (picos de 33 s) y
0,8 s en la segunda; varía con la hora, no con el tamaño del prompt.

Tercera pasada, tras cuatro ajustes más:

| Cambio | Archivo | Resultado en vivo |
|---|---|---|
| Prohibición explícita de "estoy bien", "no es nada", "puedo cuidarme solo" con una parte grave, también al final de la respuesta | `npc_ai_self.cpp` | Brazo grave: "Mi brazo derecho está gravemente herido. Necesito ayuda para moverme." Kim: "sangra bastante. Necesito ayuda para detenerlo." |
| Cabecera: "Origen de tus objetos: desconocido (no inventes donde los conseguiste)" | `npc_ai_context.cpp` | "Lo encontré en un garaje abandonado" pasó a "Lo encontré por ahí" |
| La ruta NPC_SOCIAL incluye el estado físico del que replica | `npc_ai_context.cpp` | Sarah agarrada ya no pregunta "¿dónde estás atrapada?"; dice "¿Qué? ¿Dónde?" (aún flojo) |
| Filtro de promesas tácticas ampliado: "voy a cubrir", "cubrirte", "te cubrire", "voy a por", "dejamelo", "leave it to me"... | `npc_ai_combat_social.cpp` | "Tengo una ballesta, puedo cubrirte" fue filtrado y no se dijo |
| Escenario nuevo: zombi adyacente golpea a Liam tres veces, Liam lo mata, Kim observa, batching activo | `npc_ai_live_scenarios_test.cpp` | **El lote JSON se activó**: cuatro candidatas con `slot`, `event_ids` reales y `claim_level=FACT_ONLY`; el Social Director dejó pasar una, "El zombi está muerto." |

Resultado: 21 escenarios, 27 llamadas, 0 incorrectos, latencia media 1,1 s.
Gate `[npc_ai]` sin cambios: 209 / 4228.

Queda abierto, de menor gravedad: en el lote JSON dos candidatas describieron
fallos de melé como "tiro" y "disparo" (no se pronunciaron, pero el hecho no
lleva modo de ataque explícito); Liam replicó "¡Apunta bien!" a Kim, que no
disparó; con la pregunta enrutada solo a salud Liam dijo "no veo a los demás"
teniendo a Kim al lado; y "me duele mucho" con dolor 0 al hablar de la pierna
rota. Son cuestiones de grado, no de hechos falsos graves.

## Pendiente

1. Probar en partida real con `CDDA_NPC_AI_DEBUG=1` y comparar la traza
   `five_facts_four_seconds` y el A/B de idioma contra el baseline local.
2. Salida estructurada para el lote de Combat Social donde el proveedor lo
   permita.
3. Sustituir variables de entorno por opciones de juego cuando salga de pruebas.
4. Aclarar los dos errores de `player_activities_test.cpp` con un rebuild
   limpio del proyecto de tests.
