# Cómo instalar CDDA AI (Ollama, modelo y SQLite)

Guía para otra máquina Windows (por ejemplo la de un hermano) cuando se comparte **este código** o un ejecutable ya compilado.

No copies la carpeta completa del desarrollo: pesa ~19 GB. Casi todo eso **no es el código del juego**:

| Qué | Tamaño aprox. | ¿Subirlo? |
|---|---|---|
| `.git\` (historial de Cataclysm desde 2013) | **~12 GB** | No como zip. En GitHub ya existe (CleverRaven) |
| `objwin\` (archivos de compilación) | ~4 GB | No |
| `*.pdb` (debug) | ~1 GB | No |
| Código + datos **rastreado por Git** (un snapshot) | **~1 GB** | Sí |
| `lang\po\` (traducciones fuente) | ~690 MB del snapshot | Opcional; para jugar basta `lang\mo\` |

El `.git` de 12 GB **no es vuestro trabajo de IA**. Es el historial completo de Cataclysm-DDA: **~122.600 commits** (vanilla 0.I) más unos **33 commits** de NPC AI. Git guarda cada versión de JSON, tilesets, `.po`, etc. desde 2013, empaquetada en un pack de ~12 GB.

Vuestro código extra es pequeño. Un snapshot del árbol rastreado son ~1 GB, no 12.

## Qué hay que instalar y qué no

| Componente | ¿Hace falta para jugar? | Notas |
|---|---|---|
| `cataclysm-tiles.exe` + `data\` + `gfx\` + `lang\mo\` | Sí | El juego en sí; `data\sound\CC-Sounds\` contiene la música y los efectos |
| **Ollama** + modelo **`qwen3:14b`** | Sí, para el diálogo LLM | El exe habla con `http://localhost:11434` |
| SQLite como programa aparte | **No** para jugar | La librería ya va **dentro del exe** (vcpkg `sqlite3`) |
| `sqlite3.exe` (CLI) | Opcional | Solo para inspeccionar `npc_ai.db` |
| Visual Studio + vcpkg | Solo si compilas desde el código | vcpkg instala SQLite solo |

El juego no usa un “servidor SQLite”. Crea el archivo:

`save\<Mundo>\npc_ai_memory\npc_ai.db`

Sin Ollama el juego abre; los NPC no van a generar frases con Qwen.

---

## 1. Requisitos de la máquina

- Windows 10 22H2 o posterior / Windows 11, 64 bits.
- **RAM recomendada: 32 GB.** El modelo Q4_K_M de `qwen3:14b` suele ocupar ~9 GB al estar cargado; el juego otro ~2 GB.
- Disco libre recomendado: **15-20 GB** (Ollama, modelo ~9,3 GB y juego ~0,4 GB, más margen de instalación).
- GPU NVIDIA ayuda, pero no es estrictamente obligatoria (irá más lento en CPU).
- PowerShell como administrador para `winget`.

Abre **PowerShell**.

Comprueba `winget`:

```powershell
winget --version
```

---

## 2. Instalar Ollama (PowerShell)

```powershell
winget install --id Ollama.Ollama -e --accept-source-agreements --accept-package-agreements
```

Cierra PowerShell y abre **otra** ventana (para que entre en el PATH).

Comprueba que el servicio responde:

```powershell
ollama --version
Invoke-RestMethod http://localhost:11434/api/tags
```

Si el segundo comando falla, arranca el servidor:

```powershell
ollama serve
```

En Windows, tras instalar, Ollama suele quedar en bandeja y ya escucha el puerto **11434**. El juego **solo** usa `localhost:11434` (`POST /api/generate`). No hace falta configurar otra URL.

---

## 3. Descargar el modelo que usa este proyecto

El nombre está fijado en `src/npc_ai_client.cpp`. **Tiene que llamarse exactamente así:**

```text
qwen3:14b
```

Cuantización usada en esta máquina de desarrollo: **Q4_K_M** (~9 GB). El tag de Ollama `qwen3:14b` es esa variante.

Descarga (tarda; es un solo archivo grande):

```powershell
ollama pull qwen3:14b
```

Verifica:

```powershell
ollama list
ollama show qwen3:14b
```

En `ollama list` debe aparecer `qwen3:14b`. Si el nombre es otro (`qwen2.5:14b`, `qwen3:8b`, un Modelfile propio), el juego **no lo encontrará**.

Prueba rápida (opcional):

```powershell
ollama run qwen3:14b "Responde solo: hola"
```

Sal con `/bye`.

### Parámetros que envía el juego (no hace falta copiarlos a un Modelfile)

El exe los manda en cada petición. No hace falta editar el Modelfile de Ollama.

| Parámetro | Valor |
|---|---|
| modelo | `qwen3:14b` |
| `stream` / `think` | `false` / `false` |
| `keep_alive` | `30m` |
| `temperature` | `0.4` |
| `top_p` | `0.85` |
| `top_k` | `20` |
| `repeat_penalty` | `1.1` |
| `num_predict` | `192` |
| `num_ctx` | `16384` |
| `seed` | `1` |
| stops | `<\|im_start\|>`, `<\|im_end\|>` |

`num_ctx=16384` es intencional: no uses la ventana completa de 40k del modelo (va más lento y come más RAM).

---

## 4. SQLite

### Para jugar

No instales nada. El exe ya enlaza `sqlite3.lib`. Al hablar con un NPC se crea o actualiza:

```text
save\<nombre-del-mundo>\npc_ai_memory\npc_ai.db
```

También puede haber archivos paralelos `npc_<id>.memory` (texto). Eso es normal.

### Herramienta opcional: CLI `sqlite3` (PowerShell)

Sirve para mirar la base a mano. **No** la necesita el juego.

```powershell
winget install --id SQLite.SQLite -e --accept-source-agreements --accept-package-agreements
```

Cierra y abre PowerShell. Ejemplo (ajusta la ruta del mundo):

```powershell
sqlite3 --version
sqlite3 "C:\ruta\al\juego\save\Melipilla\npc_ai_memory\npc_ai.db" ".tables"
```

### Para compilar el código (vcpkg)

SQLite entra como dependencia del proyecto (`msvc-full-features\vcpkg.json`, paquete `sqlite3`). La primera compilación de Visual Studio la descarga sola. No hace falta un `winget` de SQLite para compilar.

Si quieres forzar el paquete a mano (PowerShell), con vcpkg ya instalado en `C:\vcpkg`:

```powershell
cd C:\vcpkg
.\vcpkg install sqlite3:x64-windows-static
```

La ruta de vcpkg no debe tener espacios (`C:\vcpkg` sí; `C:\dev test\vcpkg` no).

---

## 5. Cómo subir solo el código (sin los 12 GB)

**No** hagáis `git push` a un repositorio GitHub **vacío** desde esta carpeta: Git intentaría mandar los 122.000 commits y esos 12 GB.

Tampoco subáis un zip de `C:\CDDA-AI\Cataclysm-DDA` entero.

### Opción A (recomendada): fork de Cataclysm-DDA con rama pública saneada

GitHub **ya tiene** el historial vanilla. Vosotros solo subís los ~33 commits de IA.

1. En el navegador: forkead [CleverRaven/Cataclysm-DDA](https://github.com/CleverRaven/Cataclysm-DDA) a vuestra cuenta (puede ser privado).
2. Publicad una rama saneada basada en CDDA 0.I. En este proyecto la rama pública es `cdda-ai-0.I`; no se publica la rama de trabajo ni se modifica `master`:

```powershell
cd C:\CDDA-AI\Cataclysm-DDA
git remote add fork https://github.com/TU_USUARIO/Cataclysm-DDA.git
git push -u fork cdda-ai-0.I
```

Sustituid `TU_USUARIO`. Si el fork es privado, el hermano necesita invitación.

El hermano clona **solo el snapshot** (cientos de MB, no 12 GB):

```powershell
git clone --depth 1 --branch cdda-ai-0.I https://github.com/TU_USUARIO/Cataclysm-DDA.git
```

### Opción B: zip solo del código, sin `.git`

Generad el snapshot desde la rama pública saneada, sin `.git`, `objwin`, `.pdb`, `.exe`, `save`, `config` ni logs. Es más seguro usar todos los archivos rastreados de esa rama que copiar una lista manual incompleta.

Para compilar con Visual Studio deben estar, como mínimo: `src`, `tests`, `data`, `gfx`, `pch`, `tools\format`, `build-scripts`, los archivos rastreados de `msvc-full-features`, `lang\po\es_ES.po`, `doc`, `CMakeLists.txt`, `LICENSE.txt` y `NPC_AI_INSTALACION.md`. Excluid `vcpkg_installed`, `.vs`, `*.user` y artefactos de compilación.

Los ~690 MB de todos los idiomas en `lang\po` no son necesarios para un ZIP mínimo español, pero `lang\po\es_ES.po` sí es necesario: la compilación Release genera `lang\mo\es_ES\LC_MESSAGES\cataclysm-dda.mo` mediante `build-scripts\compile_mo_fast.ps1`.

### Qué no va en ningún envío

- `.git\`
- `objwin\`
- `msvc-full-features\vcpkg_installed\` y `msvc-full-features\.vs\`
- `*.pdb`, exe de tests, `cataclysm-tiles.exe` si subís código para compilar
- `save\`, `config\`
- logs `npc_ai_*.txt`
- `save-backup-*\`, `logs_prueba*\`, bases `npc_ai.db` personales

### Compilar en el PC destino

1. Instala Git, Visual Studio (carga **Desktop development with C++**) y vcpkg. Guía vanilla: `doc/c++/COMPILING-VS-VCPKG.md`.
2. Clona el fork (opción A) o descomprime el zip (opción B).
3. Abre `msvc-full-features\Cataclysm-vcpkg-static.sln`.
4. Configuración **Release | x64**, proyecto `Cataclysm-vcpkg-static`.
5. Compila. El exe queda en la raíz: `cataclysm-tiles.exe`.
6. Instala Ollama + `qwen3:14b` como arriba **antes** de esperar diálogo LLM.

Comandos típicos de vcpkg (una vez, PowerShell):

```powershell
cd C:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat -disableMetrics
.\vcpkg integrate install
```

---

## 6. Si compartís solo el juego (sin compilar)

Copiad a una carpeta nueva **solo**:

- `cataclysm-tiles.exe`
- `data\` (incluye `data\sound\CC-Sounds\` con música y efectos; podéis omitir `data\cache` y `data\screenshots`)
- `gfx\`
- `lang\mo\`
- `NPC_AI_INSTALACION.md`
- `LICENSE.txt`
- `config\` (opcional; es preferible dejar que el juego cree uno nuevo para evitar preferencias personales)
- `save\` (opcional: la partida)

Comprimid esa carpeta y enviadla. En una configuración nueva, si `CC-Sounds` está presente el juego lo selecciona por defecto y usa volumen de música 100. Si copiáis un `config\options.json`, comprobad que `SOUNDPACKS` sea `CC-Sounds` y `MUSIC_VOLUME` sea mayor que 0. En el PC destino: descomprimir, instalar Ollama + `qwen3:14b`, ejecutar `cataclysm-tiles.exe` **con Ollama ya en marcha**.

---

## 7. Comprobación rápida

En PowerShell, con Ollama abierto:

```powershell
# 1) Ollama vivo
Invoke-RestMethod http://localhost:11434/api/tags | ConvertTo-Json -Depth 5

# 2) El modelo existe
ollama list | Select-String "qwen3:14b"

# 3) El juego puede hablar (el nombre del modelo tiene que coincidir)
```

Luego en el juego: hablad con un NPC aliado. Si Ollama no está, el diálogo LLM falla; el resto del juego sigue.

Para trazas de red (opcional, en la misma sesión de PowerShell **antes** de lanzar el juego):

```powershell
$env:CDDA_NPC_AI_DEBUG = "1"
cd C:\ruta\al\juego
.\cataclysm-tiles.exe
```

Aparecerá `npc_ai_ollama_diagnostics.txt` en la carpeta del usuario del juego (en instalación portable, la misma carpeta del exe).

---

## 8. Problemas frecuentes

| Síntoma | Qué mirar |
|---|---|
| NPC no hablan con IA | `ollama list` no tiene `qwen3:14b`, o `ollama serve` no está en 11434 |
| El juego va muy lento al hablar | Normal con 14B y `num_ctx` 16384; no carguéis otro servidor LLM a la vez |
| No aparece `npc_ai.db` | Aún no hubo conversación guardada, o no hay mundo cargado |
| Compilación no encuentra `sqlite3.h` / `sqlite3.lib` | Primera build vcpkg incompleta; repetid el build Release x64 |

No hace falta instalar Python, CUDA toolkit ni un “SQLite Server” para que esto funcione.
