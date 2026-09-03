# NPC AI con Gemini Flash (rama de pruebas)

Rama: `testing/gemini-flash`, creada sobre `cdda-ai-0.I`. Añade un segundo
proveedor de LLM sin quitar Ollama. El resto del sistema (Event Stream, Combat
Social, Context Router, validación, SQLite) no cambia: solo cambia quién
responde al prompt.

## Estado

- Código escrito, **no compilado ni probado** en la máquina donde se creó la
  rama (no había Visual Studio ni vcpkg). Primer paso obligatorio: build
  Release x64 de juego y tests, y correr `[npc_ai] --rng-seed 1`.
- Sin variables de entorno el comportamiento es idéntico al de antes: Ollama
  con `qwen3:14b`.

## Configuración

Todo por variables de entorno. **La clave nunca va en el código, en el repo, ni
en `config\`.** Ponerla en la misma sesión de PowerShell desde la que se lanza
el juego o los tests:

```powershell
$env:CDDA_NPC_AI_PROVIDER = "gemini"
$env:CDDA_NPC_AI_GEMINI_API_KEY = "<tu clave de Google AI Studio>"
# Opcionales
$env:CDDA_NPC_AI_GEMINI_MODEL = "gemini-2.5-flash"     # por defecto
$env:CDDA_NPC_AI_GEMINI_MAX_TOKENS = "256"             # por defecto
$env:CDDA_NPC_AI_DEBUG = "1"                           # trazas en npc_ai_ollama_diagnostics.txt
.\cataclysm-tiles.exe
```

| Variable | Valores | Efecto |
|---|---|---|
| `CDDA_NPC_AI_PROVIDER` | `ollama` (defecto), `gemini` | Qué backend usa la cola |
| `CDDA_NPC_AI_GEMINI_API_KEY` | clave `AIza...` | Obligatoria con `gemini`; sin ella toda petición falla en local sin tocar la red |
| `CDDA_NPC_AI_GEMINI_MODEL` | nombre de modelo | Por defecto `gemini-2.5-flash` |
| `CDDA_NPC_AI_GEMINI_MAX_TOKENS` | entero | Tope de salida; Ollama usa 192 |

Las claves de AI Studio empiezan por `AIza`. Un valor que empieza por `AQ.` es
un token OAuth de corta duración, no sirve aquí.

## Qué hace el cliente Gemini

Archivo: `src/npc_ai_client.cpp`.

- `POST https://generativelanguage.googleapis.com/v1beta/models/<modelo>:generateContent`
  con cabecera `x-goog-api-key`. HTTPS vía WinHTTP, mismo transporte que Ollama
  (helper común `winhttp_post`). Sigue siendo solo Windows.
- El `system_prompt` va como `system_instruction`; el prompt como único turno
  de usuario. Sampling igual que Ollama: temperature 0.4, topP 0.85, topK 20,
  seed 1.
- **Razonamiento desactivado** (`thinkingBudget: 0`). Gemini 2.5 piensa por
  defecto y eso multiplica latencia y coste sin aportar nada a frases cortas.
- `finishReason == MAX_TOKENS` se marca como `context_truncated`, que Combat
  Social ya trata como salida inservible. `usageMetadata` rellena
  `prompt_eval_count` y `eval_count` para las métricas existentes.
- Ante HTTP 429 o 503 entra en backoff de 20 s: las peticiones siguientes
  fallan al instante en vez de martillear la API. La cola ya trata un fallo
  como línea perdida, así que solo baja el volumen social.
- Se conserva el guard de bytes de Ollama (~15,6 KB de entrada) como tope de
  coste y porque los constructores de prompt están presupuestados para él.
- El diagnóstico con `CDDA_NPC_AI_DEBUG=1` escribe `=== GEMINI REQUEST ===` en
  el mismo archivo que Ollama. La clave no se escribe nunca; solo
  `api_key=present|MISSING`.

## Qué no cambia

- `ask_ollama` y sus tests siguen intactos. El executor por defecto de la cola
  pasa de `ask_ollama` a `ask_llm`, que despacha según `CDDA_NPC_AI_PROVIDER`.
- Prompts, validadores y presupuestos están afinados para Qwen. Hay que repetir
  el A/B de idioma y la traza `five_facts_four_seconds` con Gemini antes de dar
  nada por bueno.

## Pendiente antes de considerarlo jugable

1. Compilar y pasar `[npc_ai] --rng-seed 1` y el nuevo test
   `[npc_ai_gemini]`.
2. Una interacción real con `CDDA_NPC_AI_DEBUG=1` y revisar latencia y
   `finishReason`.
3. Comprobar cuota: el tier gratuito limita peticiones por minuto y Combat
   Social puede superarlo con varios NPC. Con tier de pago, fijar un tope de
   gasto en la consola de Google.
4. Salida estructurada (`responseSchema`) para el lote de Combat Social: hoy
   se valida a mano, con esquema forzado bajarían los descartes por JSON roto.
5. Sustituir las variables de entorno por opciones de juego cuando el
   proveedor salga de pruebas.
