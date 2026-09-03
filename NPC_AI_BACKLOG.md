# NPC AI — Backlog de ideas y análisis previo

Archivo de memoria del proyecto. No es un plan de sesión: recoge peticiones del
jugador, análisis técnico previo y decisiones tomadas, para que cualquier sesión
futura arranque sin repetir la investigación.

No borrar entradas cerradas: marcarlas.

---

## IDEA 1 — Arrastrar a un NPC herido hasta un lugar seguro

**Estado:** propuesta, analizada, NO implementada
**Fecha:** 29/08/2026
**Origen:** petición del jugador tras ver a Zachary con el brazo izquierdo roto y
las piernas dañadas, incapaz de caminar, en mitad de un combate.

### Qué se quiere

Poder arrastrar a un NPC gravemente herido, igual que hoy se arrastran muebles
con la tecla `G`, para meterlo en una casa o llevarlo a un sitio seguro.

### Hallazgo principal: el mecanismo YA EXISTE, en sentido inverso

Los zombis agarradores ya arrastran personajes. Ese código es el modelo directo
a reutilizar y está probado en producción.

**Estado del arrastre** (`src/monster.h:543`):

```cpp
character_id dragged_foe_id; // id of character being dragged by the monster
```

**Bucle de arrastre por turno** (`src/monmove.cpp:1278-1285`):

```cpp
if( has_effect( effect_dragging ) && dragged_foe != nullptr ) {
    if( !dragged_foe->has_effect( effect_grabbed ) ) {
        dragged_foe = nullptr;
        remove_effect( effect_dragging );
    } else if( drag_to != pos_abs() && creatures.creature_at( drag_to ) == nullptr ) {
        dragged_foe->move_to( drag_to );
    }
}
```

**Revalidación defensiva cada turno** (`src/monmove.cpp:1301-1321`,
`monster::find_dragged_foe()`): se busca la víctima por id en cada uso, no se
guarda un puntero. Si murió o dejó de ser válida, se limpia el id y se retira el
efecto. Este patrón debe copiarse tal cual; evita punteros colgantes.

**Inicio del arrastre** (`src/monattack.cpp:2610-2617`): tras un agarre exitoso
se asigna `dragged_foe_id` y se añade `effect_dragging`.

**Persistencia** (`src/savegame_json.cpp:2607` y `2696`): `dragged_foe_id` ya se
guarda y se carga.

### Hallazgo secundario: el enum de agarre ya contempla NPC

`src/enums.h:264-271`:

```cpp
enum class object_type : int {
    NONE, ITEM, ACTOR, PLAYER, NPC, MONSTER, VEHICLE, TRAP, FIELD, TERRAIN, ...
};
```

Hoy la tecla `G` solo usa `FURNITURE`, `FURNITURE_ON_VEHICLE` y `VEHICLE`
(`src/handle_action.cpp:713-792`), pero el valor `NPC` ya existe. No hace falta
crear una categoría nueva.

### Qué habría que construir

1. **Estado de arrastre en el personaje.** Un `character_id` del NPC arrastrado,
   con el mismo patrón de revalidación por id de `find_dragged_foe()`. Nunca un
   puntero persistente.

2. **Extender la tecla G.** En `src/handle_action.cpp`, al pulsar `G` sobre una
   casilla ocupada por un NPC aliado elegible, ofrecer agarrarlo. Mantener el
   comportamiento actual de muebles y vehículos intacto.

3. **Condición de elegibilidad.** No se debe poder arrastrar a un NPC sano. Debe
   exigirse un estado real del juego: piernas rotas o gravemente dañadas,
   inconsciente, o incapacitado para moverse. Hay que decidir el criterio exacto
   usando fuentes vanilla, no una heurística inventada.

4. **Movimiento.** Al moverse el jugador, mover al NPC arrastrado con el mismo
   patrón: destino libre de criaturas, y `move_to`. Coste de movimiento penalizado
   en función del peso del NPC más su equipo.

5. **Condiciones de suelta.** Pulsar `G` de nuevo, que el NPC deje de estar
   incapacitado, que muera, que un enemigo lo agarre, o que el jugador quede
   agarrado o inmovilizado.

6. **Persistencia.** Guardar y cargar el id, siguiendo lo que ya hace
   `savegame_json.cpp` para `dragged_foe_id`.

7. **Conflicto con los agarradores.** Si un zombi ya tiene agarrado al NPC, el
   arrastre del jugador debe fallar o disputarse, no ejecutarse en paralelo.

### Ampliación posible, mayor alcance

Que un NPC pueda arrastrar a otro NPC herido sin intervención del jugador. Es lo
que más aportaría a la sensación de grupo vivo, pero exige decisión autónoma y
queda fuera de una primera versión.

### Riesgos

- No romper el arrastre de muebles ni de vehículos, que funciona.
- No introducir punteros a criaturas que puedan quedar colgantes; usar id y
  revalidar, como hace el código vanilla.
- El arrastre no debe permitir mover a un NPC a través de sitios por los que no
  podría pasar por sí mismo.

---

## HALLAZGO 1 — La movilidad no llega al contexto del NPC

**Estado:** corregido parcialmente, 03/09/2026. `render_self_snapshot` añade
`movilidad={puede_caminar; movilidad_reducida; piernas_rotas; brazos_rotos;
necesita_ferula_o_medico; miembros_rotos}` derivado de `is_limb_broken`, y el
Context Router enruta "¿puedes caminar?", "¿necesitas ayuda?" y similares a
HEALTH. Verificado en vivo: con la pierna izquierda a 0 HP Liam responde
"puedo caminar, pero con dificultad. Mi pierna izquierda está rota". Falta el
criterio vanilla de incapacidad total y que el NPC lo comunique sin que se le
pregunte (habla espontánea).
**Fecha:** 29/08/2026

Zachary tenía el brazo izquierdo roto, con el mensaje del juego *"Está roto.
Necesitas una férula o atención quirúrgica"*, y ambas piernas dañadas hasta el
punto de no poder caminar. No lo comunicó en ningún momento.

El trabajo de Self State de la sesión del 29/08 cubrió HP por parte del cuerpo,
dolor y sangrado, pero **no la movilidad**. Un NPC que no puede caminar debería
poder decirlo, y es además el disparador natural de la idea de arrastre: sin ese
aviso, el jugador no sabe que hace falta sacarlo de ahí.

Añadir al contexto de salud: capacidad de movimiento, miembros rotos, y
necesidad de férula o atención médica.
