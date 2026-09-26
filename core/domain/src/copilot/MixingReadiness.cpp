#include "daw/domain/copilot/MixingReadiness.h"

#include <algorithm>

namespace daw::domain::copilot
{

const std::vector<MixingNeed>& mixingNeeds()
{
    static const std::vector<MixingNeed> needs{
        // --- the verbs
        {"régler le volume de chaque piste", {"track.set_volume"}, "le premier geste du mixage"},
        {"placer chaque piste dans le champ stéréo", {"track.set_pan"}, "l'image stéréo"},
        {"couper une piste", {"track.set_muted"}, "comparer avec et sans"},
        {"écouter une piste seule", {"track.set_solo"}, "juger une piste sans les autres"},
        {"créer un bus", {"bus.add"}, "grouper, partager un effet"},
        {"envoyer une piste dans un bus", {"track.set_output"}, "traiter un groupe d'un seul geste"},
        {"envoyer une partie d'une piste vers un bus", {"track.set_send"}, "une réverbe partagée"},
        {"régler le master", {"track.set_volume"}, "le niveau de sortie, par l'identifiant du master"},
        {"insérer un effet sur une piste, un bus ou le master",
         {"plugin.insert"},
         "EQ, compression, limiteur"},
        {"régler un paramètre d'effet", {"plugin.set_parameter"}, "façonner le son"},
        {"contourner un effet", {"plugin.set_bypassed"}, "comparer avec et sans l'effet"},
        {"défaire tout un essai d'un coup", {"commands.execute"}, "une demande = un Ctrl+Z"},

        // --- knowing what the verbs act on
        {"lire l'état du mixage : bus, sorties, envois, solo, master",
         {"state.get"},
         "savoir d'où l'on part"},
        {"connaître les paramètres d'un effet : noms, unités, plages, valeurs actuelles",
         {"plugin.parameters"},
         "l'état ne liste que les paramètres déjà touchés, par identifiant et en valeur normalisée : une IA "
         "ne "
         "sait pas que « 0,42 » est une fréquence de coupure à 800 Hz"},
        {"disposer d'effets de base connus quelle que soit la machine : EQ, compresseur, limiteur",
         {"mix.stock_effects"},
         "les plugins installés varient d'une machine à l'autre et leurs paramètres n'ont pas de sens "
         "commun"},

        // --- measuring, to judge
        {"mesurer le niveau de chaque piste et du master maintenant", {"mix.levels"}, "voir ce qui sonne"},
        {"mesurer crête et RMS sur un passage choisi, par un rendu",
         {"mix.measure"},
         "300 ms en direct ne disent rien d'un refrain ; les totaux d'un rendu existent dans le moteur mais "
         "ne "
         "sont pas exposés au copilote"},
        {"mesurer la sonie intégrée (LUFS) et la crête vraie",
         {"mix.loudness"},
         "la cible d'un master se donne en LUFS, pas en RMS"},
        {"mesurer le spectre de chaque piste et du master",
         {"mix.spectrum"},
         "équilibrer les graves et les aigus, choisir où égaliser"},
        {"repérer les pistes qui se masquent",
         {"mix.masking"},
         "deux pistes dans la même bande de fréquences"},
        {"mesurer la corrélation et la largeur stéréo", {"mix.stereo"}, "la compatibilité mono, la largeur"},
        {"mesurer la dynamique : facteur de crête, réduction de gain d'un compresseur",
         {"mix.dynamics"},
         "savoir si une piste est trop compressée ou pas assez"},

        // --- what a mix also uses
        {"automatiser un réglage dans le temps", {"automation.write"}, "hors périmètre de la S11"},
        {"déclencher un compresseur par une autre piste (sidechain)",
         {"track.set_sidechain"},
         "le kick qui fait respirer la basse"},
        {"comparer à un morceau de référence", {"mix.reference"}, "juger un mix contre un autre"},
    };
    return needs;
}

std::vector<MixingNeed> mixingGaps(const std::vector<std::string>& offered)
{
    std::vector<MixingNeed> gaps;
    for (const auto& need : mixingNeeds())
    {
        const bool met =
            std::any_of(need.satisfiedBy.begin(),
                        need.satisfiedBy.end(),
                        [&offered](const std::string& name)
                        { return std::find(offered.begin(), offered.end(), name) != offered.end(); });
        if (!met)
            gaps.push_back(need);
    }
    return gaps;
}

} // namespace daw::domain::copilot
