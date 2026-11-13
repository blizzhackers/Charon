"""Directly author YARB's policy networks. No samples, optimizer, fitting or randomness.

    python tools/author_policy_weights.py           # write scripts/lib/*_weights.lua
    python tools/author_policy_weights.py --check   # verify the checked-in artifacts are exact

Every network is `inputs -> hidden ReLU -> 1 score`, evaluated per candidate by
the native `policy` binding. A network is written as named equations:

    score = bias + sum(BASE[f] * f) + sum(out * ReLU(sum(coef[f] * f) + gate_bias))

The first hidden units pass each named input through unchanged (inputs are
normalized to 0..1, so ReLU is the identity on them) and carry the BASE
preferences. One further hidden unit per GATE encodes an interaction, so the
same observation can favor different candidates.
"""
from pathlib import Path
import argparse

# Candidates are described by expected outcomes (see lib/neural_combat.lua):
#   tempo            share of the best damage rate on offer that this action keeps
#   finish           share of the target's remaining life it removes now
#   progress         share of the gap to casting range that an approach closes
#   lethality_after  x/(1+x) of the share x of our remaining life that the monsters
#                    in reach take over the next seconds from where the action leaves us
#   outlast          share of what a fight may cost us (life plus healing in hand)
#                    that is left once the threats in reach are dead, fighting
#                    from where the action leaves us; 0 when it is not decided there
#   thrift           damage per mana of this cast as a share of the best the mana
#                    could buy (the leading volley at its distance, Static Field
#                    on the target in reach); 1 for what costs no mana
#   missile_after    projectile danger where the action leaves us, scaled by how
#                    much of our remaining life such a missile takes
#   engaged          a fight is on (a target, not the travel-time missile check)
COMBAT = dict(
    name='combat', version='combat-v4', bias=0,
    features=(
        'tempo finish progress lethality_after outlast missile_after move time mana mana_cost thrift '
        'nova target_chill kite_intent continuity target_priority wait engaged').split(),
    base=dict(tempo=6, finish=2, progress=2, missile_after=-14,
              move=-.4, time=-.5, mana_cost=-.5, nova=-.1, continuity=.15,
              target_priority=.3, wait=-.2),
    # (name, affine input coefficients, bias, output coefficient)
    gates=[
        # Exposure is free up to a ninth of what life is left (0.1): weak monsters
        # are no reason to move. From a quarter of it (0.2) the cost steepens, so
        # that a real escape wins near 0.3: in live play nearly every large loss
        # of life followed a choice made at 0.3 or more. Past two thirds (0.4)
        # it steepens again. A fight that is won where we stand moves all three
        # thresholds up by 0.6 of the life budget it leaves (outlast): kiting
        # Andariel took minutes and more potions than standing in her reach
        # for the six seconds that Static Field and a few volleys need.
        ('exposure', dict(lethality_after=1, outlast=-.6), -.1, -14),
        ('heavy_exposure', dict(lethality_after=1, outlast=-.6), -.2, -30),
        ('grave_exposure', dict(lethality_after=1, outlast=-.6), -.4, -14),
        # Standing idle with a target in a fight achieves nothing.
        ('idle_in_a_fight', dict(wait=1, engaged=1), -1, -1.5),
        ('scarce_mana', dict(mana_cost=1, mana=-.35), .1, -3),
        # Below half a pool, mana goes to what it buys most with: Andariel was
        # met again and again without the nine mana for Static Field, spent a
        # moment earlier on a volley from where one bolt in ten connects.
        ('wasted_mana', dict(mana=-1, thrift=-1), .5, -12),
        ('chill_before_kite', dict(nova=1, target_chill=1, kite_intent=1, missile_after=-1), -2, 24),
    ])

# Heuristic route utility between optional Tower preparation and the main route;
# the XP inputs are comparable priors, not measured rates.
STRATEGY = dict(
    name='strategy', version='strategy-v2', bias=0,
    features=(
        'progress tower bolt_gap control_gap level_gap xp_value travel_cost reset_cost '
        'first_clear commitment failures tower_time').split(),
    base=dict(progress=3, tower=.5, xp_value=2, travel_cost=-1, reset_cost=-1, commitment=2),
    gates=[
        ('tower_control_preparation', dict(tower=1, control_gap=1), -1, 8),
        ('tower_bolt_preparation', dict(tower=1, bolt_gap=1), -1, 4),
        ('tower_level_preparation', dict(tower=1, level_gap=1), -1, 6),
        ('first_tower_clear', dict(tower=1, first_clear=1), -1, 1.5),
        ('tower_time_cost', dict(tower=1, tower_time=1), -1, -2),
        ('tower_failure_cost', dict(tower=1, failures=1), -1, -3),
        ('unprepared_progress', dict(progress=1, control_gap=1, bolt_gap=1), -1, -6),
    ])

NETWORKS = (COMBAT, STRATEGY)


def artifact(net):
    features = net['features']
    assert len(set(features)) == len(features), 'duplicate feature name'
    assert set(net['base']) <= set(features), 'BASE names an unknown feature'
    units = [(feature, {feature: 1}, 0, net['base'].get(feature, 0)) for feature in features]
    units += net['gates']
    parameters = len(units) * len(features) + 2 * len(units) + 1
    number = lambda n: format(n, '.17g')
    lines = ['-- Generated by tools/author_policy_weights.py; edit the authoring source.',
             f'-- Directly programmed {len(features)} -> {len(units)} ReLU -> 1; '
             f'{parameters} parameters. No training.',
             'return {', f"  version = '{net['version']}',", '  features = {']
    lines += [f"    '{feature}'," for feature in features]
    lines += ['  },', '  w1 = {']
    for name, coefficients, _, _ in units:
        assert set(coefficients) <= set(features), f'{name} names an unknown feature'
        lines += ['    -- ' + name, '    ' + ', '.join(number(coefficients.get(f, 0)) for f in features) + ',']
    lines += ['  },', '  b1 = {' + ', '.join(number(unit[2]) for unit in units) + '},',
              '  w2 = {' + ', '.join(number(unit[3]) for unit in units) + '},',
              f"  b2 = {net['bias']!r},", '}', '']
    summary = (f"{net['version']}: {len(features)} inputs, {len(units)} ReLU units, 1 score, "
               f'{parameters} authored parameters')
    return '\n'.join(lines), summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='verify the artifacts instead of writing them')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1] / 'scripts/lib'
    for net in NETWORKS:
        path = root / f"{net['name']}_weights.lua"
        text, summary = artifact(net)
        if args.check:
            assert path.read_text(encoding='utf-8') == text, f'{path.name} differs from its authoring source; regenerate'
        else:
            path.write_text(text, encoding='utf-8', newline='\n')
        print(summary)
