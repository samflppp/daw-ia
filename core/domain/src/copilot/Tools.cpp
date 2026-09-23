#include "daw/domain/copilot/Tools.h"

#include "daw/domain/project/ProjectState.h"

#include <algorithm>
#include <utility>

namespace daw::domain::copilot
{
namespace
{

// --- the small pieces a schema is made of ----------------------------------

Value field(std::string type, std::string description)
{
    return Value::object({{"type", Value{std::move(type)}}, {"description", Value{std::move(description)}}});
}

Value identifier(std::string description)
{
    return Value::object({{"type", Value{std::string{"string"}}},
                          {"description", Value{std::move(description)}},
                          {"minLength", Value{static_cast<std::int64_t>(Ulid::textLength)}},
                          {"maxLength", Value{static_cast<std::int64_t>(Ulid::textLength)}}});
}

// The identifier of something the caller is about to create.
//
// A ULID is twenty-six characters of Crockford base32, and asking a language
// model to invent one is asking for payloads the domain will refuse: the
// alphabet leaves out I, L, O and U, and nothing in a sentence says which
// twenty-six characters to pick. So the model writes a name it chose —
// "$new:basse" — and the copilot process mints the ULID before the payload
// reaches the bus.
//
// The rule is untouched: the caller still supplies the identifier, and the bus
// still engenders none. What changed is which part of the caller writes it.
// The same token used twice in one request names the same thing, which is what
// makes "add a Bass track and put Vital on it" work at all.
Value newIdentifier(std::string description)
{
    return Value::object(
        {{"type", Value{std::string{"string"}}},
         {"description",
          Value{std::move(description) + " Écrivez un nom court précédé de $new:, par exemple $new:basse. "
                                         "Le même nom dans la même requête désigne la même chose."}},
         {"pattern", Value{std::string{"^\\$new:[a-z0-9_-]{1,32}$|^[0-9A-HJKMNP-TV-Z]{26}$"}}}});
}

Value number(std::string description, double minimum, double maximum)
{
    return Value::object({{"type", Value{std::string{"number"}}},
                          {"description", Value{std::move(description)}},
                          {"minimum", Value{minimum}},
                          {"maximum", Value{maximum}}});
}

Value integer(std::string description, std::int64_t minimum, std::int64_t maximum)
{
    return Value::object({{"type", Value{std::string{"integer"}}},
                          {"description", Value{std::move(description)}},
                          {"minimum", Value{minimum}},
                          {"maximum", Value{maximum}}});
}

Value arrayOf(Value items, std::string description)
{
    return Value::object({{"type", Value{std::string{"array"}}},
                          {"description", Value{std::move(description)}},
                          {"minItems", Value{std::int64_t{1}}},
                          {"items", std::move(items)}});
}

Value schema(Value::Object properties, std::vector<std::string> required)
{
    Value::Array names;
    names.reserve(required.size());
    for (auto& name : required)
        names.push_back(Value{std::move(name)});

    return Value::object({{"type", Value{std::string{"object"}}},
                          {"properties", Value::object(std::move(properties))},
                          {"required", Value::array(std::move(names))},
                          {"additionalProperties", Value{false}}});
}

Value pluginSchema()
{
    // The whole instance, because that is what plugin.insert reads. bypassed
    // is required and not defaulted: a command whose payload leaves a field to
    // the reader is a command that means two things.
    return Value::object(
        {{"type", Value{std::string{"object"}}},
         {"properties",
          Value::object(
              {{"id", newIdentifier("Identifiant de cette instance de plugin.")},
               {"bypassed", field("boolean", "Faux pour un plugin qui joue. Mettez faux par défaut.")},
               {"ref",
                Value::object(
                    {{"type", Value{std::string{"object"}}},
                     {"properties",
                      Value::object({{"format", field("string", "VST3 ou CLAP, tel que l'état le donne.")},
                                     {"identifier",
                                      field("string",
                                            "Identifiant stable du plugin, tel que la "
                                            "machine le déclare. Jamais un chemin.")},
                                     {"name", field("string", "Nom lisible du plugin.")}})},
                     {"required",
                      Value::array({Value{std::string{"format"}},
                                    Value{std::string{"identifier"}},
                                    Value{std::string{"name"}}})},
                     {"additionalProperties", Value{false}}})}})},
         {"required",
          Value::array(
              {Value{std::string{"id"}}, Value{std::string{"ref"}}, Value{std::string{"bypassed"}}})},
         {"additionalProperties", Value{false}}});
}

Tool make(std::string name, std::string summary, Value payloadSchema)
{
    Tool tool{};
    tool.name = std::move(name);
    tool.summary = std::move(summary);
    tool.schema = std::move(payloadSchema);
    return tool;
}

// A command the application calls and the model cannot: capturing the state of
// a plugin means holding its bytes, and the copilot holds none. It is in the
// table because the table describes the registry, and out of the list the
// model is given because a tool nobody can call well is a tool called badly.
Tool hidden(Tool tool)
{
    tool.offeredToModel = false;
    return tool;
}

} // namespace

Value Tool::toValue() const
{
    return Value::object({{"name", Value{name}}, {"description", Value{summary}}, {"input_schema", schema}});
}

Value toValue(const std::vector<Tool>& tools)
{
    Value::Array items;
    items.reserve(tools.size());
    for (const auto& tool : tools)
    {
        if (tool.offeredToModel)
            items.push_back(tool.toValue());
    }

    return Value::array(std::move(items));
}

std::vector<Tool> builtinTools()
{
    const auto trackId = identifier("Identifiant de la piste.");
    const auto clipId = identifier("Identifiant de la ligne d'un pattern, telle que l'état la nomme.");
    const auto noteId = identifier("Identifiant de la note.");
    const auto pluginId = identifier("Identifiant de l'instance de plugin.");
    const auto pointId = identifier("Identifiant du point de tempo.");
    const auto patternId = identifier("Identifiant du pattern.");
    const auto placementId = identifier("Identifiant d'un placement, tel que l'état le nomme.");

    std::vector<Tool> tools;

    // --- tracks
    tools.push_back(make(
        "track.add",
        "Ajoute une piste vide à la fin de la liste.",
        schema(
            {{"trackId", newIdentifier("Identifiant de la piste à créer.")},
             {"name", field("string", "Nom de la piste.")},
             {"volumeDb",
              number("Volume initial en décibels.", ProjectState::minVolumeDb, ProjectState::maxVolumeDb)}},
            {"trackId", "name", "volumeDb"})));

    tools.push_back(make("track.remove",
                         "Retire une piste, avec ses plugins et ce qu'elle joue dans chaque pattern.",
                         schema({{"trackId", trackId}}, {"trackId"})));

    tools.push_back(
        make("track.rename",
             "Renomme une piste.",
             schema({{"trackId", trackId}, {"name", field("string", "Nouveau nom.")}}, {"trackId", "name"})));

    tools.push_back(make("track.reorder",
                         "Déplace une piste à une autre place dans la liste.",
                         schema({{"trackId", trackId},
                                 {"index", integer("Position visée, 0 étant la première piste.", 0, 4096)}},
                                {"trackId", "index"})));

    tools.push_back(make(
        "track.set_volume",
        "Règle le volume d'une piste, en décibels absolus. Pour « monte de 3 dB », lire le "
        "volume actuel dans l'état et envoyer la somme.",
        schema({{"trackId", trackId},
                {"volumeDb",
                 number("Volume visé en décibels.", ProjectState::minVolumeDb, ProjectState::maxVolumeDb)}},
               {"trackId", "volumeDb"})));

    tools.push_back(make("track.set_pan",
                         "Place une piste dans le champ stéréo.",
                         schema({{"trackId", trackId},
                                 {"pan",
                                  number("-1 tout à gauche, 0 au centre, +1 tout à droite.",
                                         ProjectState::minPan,
                                         ProjectState::maxPan)}},
                                {"trackId", "pan"})));

    tools.push_back(
        make("track.set_muted",
             "Coupe ou rétablit une piste. Différent du volume et du contournement d'un plugin.",
             schema({{"trackId", trackId}, {"muted", field("boolean", "Vrai pour couper la piste.")}},
                    {"trackId", "muted"})));

    tools.push_back(
        make("track.set_channel_pitch",
             "Règle la hauteur fixe d'une piste dans le channel rack : la note qu'elle joue quand un "
             "pas est allumé. Ne change aucune note déjà écrite.",
             schema({{"trackId", trackId},
                     {"pitch",
                      integer("Hauteur MIDI, 60 = do central.",
                              ProjectState::lowestChannelPitch,
                              ProjectState::highestChannelPitch)}},
                    {"trackId", "pitch"})));

    // --- patterns, placements and rows
    //
    // Le contenu et la position sont séparés : un pattern porte ce qui se joue,
    // un placement dit où. Modifier un pattern posé huit fois est une seule
    // commande, parce que les huit placements ne portent aucune note.
    tools.push_back(make("pattern.create",
                         "Crée un pattern vide. Un pattern porte le contenu, pas sa position : "
                         "il faut le poser avec pattern.place pour qu'il sonne.",
                         schema({{"patternId", newIdentifier("Identifiant du pattern à créer.")},
                                 {"name", field("string", "Nom du pattern. Peut être vide.")},
                                 {"lengthBeats", field("number", "Longueur du pattern, en temps.")}},
                                {"patternId", "name", "lengthBeats"})));

    tools.push_back(make("pattern.place",
                         "Pose un pattern sur la timeline. Le même pattern peut être posé autant de fois que "
                         "voulu : aucune note n'est copiée, et le modifier une fois le change partout.",
                         schema({{"placementId", newIdentifier("Identifiant de ce placement.")},
                                 {"patternId", patternId},
                                 {"startBeats", field("number", "Position sur la timeline, en temps.")}},
                                {"placementId", "patternId", "startBeats"})));

    tools.push_back(
        make("pattern.add_track",
             "Ouvre la ligne d'une piste dans un pattern. La ligne est vide : les notes s'ajoutent "
             "ensuite avec note.add, en nommant le clipId créé ici.",
             schema({{"patternId", patternId},
                     {"clipId", newIdentifier("Identifiant de la ligne à créer.")},
                     {"trackId", trackId}},
                    {"patternId", "clipId", "trackId"})));

    tools.push_back(make(
        "pattern.set_length",
        "Change la longueur d'un pattern, en temps. Les notes au-delà sont gardées, "
        "jamais coupées.",
        schema({{"patternId", patternId}, {"lengthBeats", field("number", "Nouvelle longueur, en temps.")}},
               {"patternId", "lengthBeats"})));

    tools.push_back(make("pattern.rename",
                         "Renomme un pattern. Un nom vide le fait afficher par son rang.",
                         schema({{"patternId", patternId}, {"name", field("string", "Nouveau nom.")}},
                                {"patternId", "name"})));

    tools.push_back(make("pattern.remove",
                         "Supprime un pattern, ses lignes, et tous ses placements sur la timeline.",
                         schema({{"patternId", patternId}}, {"patternId"})));

    // --- the arrangement
    //
    // Un placement pose un pattern à un temps. Il ne porte ni piste ni longueur :
    // les deux sont dans le pattern. Le déplacer ou le retirer ne touche aucune
    // note, et les autres placements du même pattern ne bougent pas.
    tools.push_back(
        make("placement.move",
             "Déplace un placement sur la timeline. Le pattern et ses autres placements ne bougent pas.",
             schema({{"placementId", placementId},
                     {"startBeats", field("number", "Nouvelle position sur la timeline, en temps.")}},
                    {"placementId", "startBeats"})));

    tools.push_back(make("placement.remove",
                         "Retire un placement de la timeline. Le pattern reste, et ses autres placements "
                         "aussi.",
                         schema({{"placementId", placementId}}, {"placementId"})));

    // --- clips and notes
    tools.push_back(
        make("clip.create_midi",
             "Crée un pattern d'une seule piste et le pose sur la timeline, en une commande. Pour un "
             "pattern à plusieurs pistes, utiliser pattern.create puis pattern.add_track.",
             schema({{"trackId", trackId},
                     {"clipId", newIdentifier("Identifiant de la ligne à créer.")},
                     {"startBeats", field("number", "Position sur la timeline, en temps.")},
                     {"lengthBeats", field("number", "Longueur du pattern, en temps.")}},
                    {"trackId", "clipId", "startBeats", "lengthBeats"})));

    tools.push_back(make(
        "note.add",
        "Ajoute une note dans la ligne d'un pattern. Le payload porte la ligne et la note à plat. "
        "Le début est relatif au pattern, pas à la timeline.",
        schema({{"clipId", clipId},
                {"id", newIdentifier("Identifiant de la note à créer.")},
                {"pitch", integer("Hauteur MIDI, 60 = do central.", Note::lowestPitch, Note::highestPitch)},
                {"velocity", integer("Force de frappe.", Note::lowestVelocity, Note::highestVelocity)},
                {"startBeats", field("number", "Début dans le clip, en temps.")},
                {"lengthBeats", field("number", "Durée, en temps.")}},
               {"clipId", "id", "pitch", "velocity", "startBeats", "lengthBeats"})));

    tools.push_back(make("note.remove",
                         "Efface une note d'un clip.",
                         schema({{"clipId", clipId}, {"noteId", noteId}}, {"clipId", "noteId"})));

    tools.push_back(
        make("note.move",
             "Déplace une note en hauteur et dans le temps. Sa durée ne change pas.",
             schema({{"clipId", clipId},
                     {"noteId", noteId},
                     {"pitch", integer("Nouvelle hauteur MIDI.", Note::lowestPitch, Note::highestPitch)},
                     {"startBeats", field("number", "Nouveau début, en temps.")}},
                    {"clipId", "noteId", "pitch", "startBeats"})));

    tools.push_back(make("note.resize",
                         "Change la durée d'une note. Son début ne bouge pas.",
                         schema({{"clipId", clipId},
                                 {"noteId", noteId},
                                 {"lengthBeats", field("number", "Nouvelle durée, en temps.")}},
                                {"clipId", "noteId", "lengthBeats"})));

    tools.push_back(make(
        "note.set_velocity",
        "Change la force de frappe d'une note.",
        schema({{"clipId", clipId},
                {"noteId", noteId},
                {"velocity", integer("Nouvelle vélocité.", Note::lowestVelocity, Note::highestVelocity)}},
               {"clipId", "noteId", "velocity"})));

    tools.push_back(make(
        "note.quantize",
        "Aligne des notes sur une grille. Les notes sont nommées une à une : lire les "
        "identifiants du clip avant d'appeler. Une double-croche vaut 0.25 temps.",
        schema({{"clipId", clipId},
                {"noteIds", arrayOf(identifier("Identifiant d'une note du clip."), "Les notes à aligner.")},
                {"gridBeats",
                 field("number",
                       "Pas de la grille, en temps. 0.25 pour des "
                       "doubles-croches, 0.5 pour des croches.")}},
               {"clipId", "noteIds", "gridBeats"})));

    tools.push_back(make(
        "note.transpose",
        "Transpose des notes. Une octave vers le bas vaut -12 demi-tons.",
        schema(
            {{"clipId", clipId},
             {"noteIds", arrayOf(identifier("Identifiant d'une note du clip."), "Les notes à transposer.")},
             {"semitones", integer("Nombre de demi-tons, négatif vers le grave.", -127, 127)}},
            {"clipId", "noteIds", "semitones"})));

    // --- plugins
    tools.push_back(
        make("plugin.insert",
             "Insère un plugin dans la chaîne d'une piste. Le plugin doit exister sur cette machine : "
             "lire la liste des plugins disponibles avant d'appeler.",
             schema({{"trackId", trackId},
                     {"plugin", pluginSchema()},
                     {"index", integer("Place dans la chaîne, 0 en tête.", 0, 4096)}},
                    {"trackId", "plugin", "index"})));

    tools.push_back(make(
        "plugin.remove", "Retire un plugin de la chaîne.", schema({{"pluginId", pluginId}}, {"pluginId"})));

    tools.push_back(
        make("plugin.set_bypassed",
             "Contourne un plugin sans le retirer. Différent de couper la piste.",
             schema({{"pluginId", pluginId}, {"bypassed", field("boolean", "Vrai pour contourner.")}},
                    {"pluginId", "bypassed"})));

    tools.push_back(make("plugin.set_parameter",
                         "Règle un paramètre d'un plugin, en valeur normalisée de 0 à 1.",
                         schema({{"pluginId", pluginId},
                                 {"paramId",
                                  field("string",
                                        "Identifiant du paramètre, tel que le "
                                        "plugin le nomme.")},
                                 {"value", number("Valeur normalisée.", 0.0, 1.0)}},
                                {"pluginId", "paramId", "value"})));

    tools.push_back(hidden(make(
        "plugin.capture_state",
        "Enregistre l'état interne d'un plugin, nommé par empreinte. Réservé à l'application : "
        "le copilote n'a pas les octets.",
        schema(
            {{"pluginId", pluginId},
             {"state",
              Value::object({{"type", Value{std::string{"object"}}},
                             {"description", Value{std::string{"Référence au contenu, par empreinte."}}}})}},
            {"pluginId", "state"}))));

    // --- tempo
    tools.push_back(
        make("tempo.insert",
             "Ajoute un changement de tempo sur la timeline.",
             schema({{"pointId", newIdentifier("Identifiant du point à créer.")},
                     {"startBeats", field("number", "Position du changement, en temps.")},
                     {"beatsPerMinute",
                      number("Tempo à partir de ce point.", ProjectState::minTempo, ProjectState::maxTempo)}},
                    {"pointId", "startBeats", "beatsPerMinute"})));

    tools.push_back(make("tempo.remove",
                         "Retire un changement de tempo. Le point à l'origine ne peut pas être retiré.",
                         schema({{"pointId", pointId}}, {"pointId"})));

    tools.push_back(make(
        "tempo.set_bpm",
        "Change le tempo d'un point existant. Pour « passe le tempo à 140 », viser le point à "
        "l'origine, que l'état nomme.",
        schema({{"pointId", pointId},
                {"beatsPerMinute", number("Nouveau tempo.", ProjectState::minTempo, ProjectState::maxTempo)}},
               {"pointId", "beatsPerMinute"})));

    tools.push_back(
        make("tempo.move",
             "Déplace un changement de tempo. Le point à l'origine ne bouge pas.",
             schema({{"pointId", pointId}, {"startBeats", field("number", "Nouvelle position, en temps.")}},
                    {"pointId", "startBeats"})));

    // --- transport
    tools.push_back(make("transport.play", "Lance la lecture.", schema({}, {})));
    tools.push_back(make("transport.stop", "Arrête la lecture et ramène la tête au début.", schema({}, {})));

    tools.push_back(
        make("transport.set_position",
             "Déplace la tête de lecture.",
             schema({{"positionBeats", field("number", "Position visée, en temps.")}}, {"positionBeats"})));

    tools.push_back(make("transport.set_loop",
                         "Fait boucler la lecture sur une plage, ou arrête la boucle.",
                         schema({{"looping", field("boolean", "Vrai pour boucler.")},
                                 {"startBeats", field("number", "Début de la boucle, en temps.")},
                                 {"endBeats", field("number", "Fin de la boucle, en temps.")}},
                                {"looping", "startBeats", "endBeats"})));

    tools.push_back(make(
        "transport.set_mode",
        "Bascule entre le mode pattern (un seul pattern, seul, en boucle depuis le début) et le mode "
        "chanson (la timeline entière, tous les placements). Ramène la tête au début.",
        schema(
            {{"mode",
              Value::object(
                  {{"type", Value{std::string{"string"}}},
                   {"enum", Value::array({Value{std::string{"pattern"}}, Value{std::string{"song"}}})},
                   {"description", Value{std::string{"pattern ou song."}}}})},
             {"patternId",
              field("string",
                    "En mode pattern, l'identifiant du pattern à jouer. En mode chanson, une chaîne vide.")}},
            {"mode", "patternId"})));

    return tools;
}

Result<std::vector<Tool>> toolsFor(const CommandRegistry& registry)
{
    auto tools = builtinTools();

    for (const auto& type : registry.types())
    {
        const auto found =
            std::find_if(tools.begin(), tools.end(), [&type](const Tool& tool) { return tool.name == type; });
        if (found == tools.end())
            return fail(ErrorCode::notFound, "no tool describes the command " + type);
    }

    for (const auto& tool : tools)
    {
        if (!registry.contains(tool.name))
            return fail(ErrorCode::notFound, "the tool " + tool.name + " describes no command");
    }

    return tools;
}

} // namespace daw::domain::copilot
