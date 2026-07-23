// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/tempo_point.hpp>

namespace graphscore {

// This header's file-level overview -- the Phase 7 "normative playback
// specification" deliverable for tempo (docs/plan/02-domain-model.md:
// "Specify cubic smooth-tempo curve equations, legal control handles,
// deterministic integration/inversion tolerances, and integer sample
// rounding at boundaries") -- spans every declaration below (the constants
// through round_to_sample_count()), not only the one immediately following
// it. docs/plan/README.md's "Tempo and meter" section is the product
// decision this evaluates: "Tempo is a node-wide continuous lane anchored
// to musical positions", "Tempo points carry an explicit beat unit and
// accept 10 through 400 BPM", "Segments support step changes, linear
// interpolation, and smooth rounded curves." graphscore/domain/
// tempo_lane.hpp's TempoLane models the point/segment DATA and its
// structural validity (ordering, coverage); this header is what actually
// evaluates a segment's tempo, integrates it into elapsed real time, and
// inverts that integral back to a musical position -- the three operations
// TempoLane::segment_index_at()'s own doc comment names as deferred here.
//
// -- Why this lives in graphscore_core, and why floating point is used --
// Placed in core rather than domain for exactly the reason
// DeterministicPrng (core/deterministic_prng.hpp) is: the ADR 0003 runtime
// closure (graphscore_runtime and everything it depends on) can see
// graphscore_core but never graphscore_domain, and the writer and runtime
// must be able to reproduce compatible tempo/timing results from the same
// TempoPoint data at playback time, not only at authoring time.
//
// This module is the one, narrow, explicitly product-approved exception to
// this project's "never floating point for score structure" rule
// (AGENTS.md; docs/plan/02-domain-model.md's "Exact rational musical
// positions and durations; avoid floating-point equality for score
// structure"). Every function below keeps its Rational-typed musical
// positions and durations exact on the way in and on the way out --
// nowhere does this header hand a caller a position or duration that lost
// precision through a float round trip it didn't ask for. What is
// genuinely irreducible to exact rational arithmetic is integrating a
// cubic curve into elapsed real time and inverting that integral: neither
// operation has an exact rational closed form in general (see "Why
// kSmooth integrates numerically" below), and real time itself -- seconds
// of wall-clock audio -- is not a musical quantity this project's
// "exact Rational" convention was ever meant to cover. `double` is used
// for exactly that irreducible internal arithmetic and nowhere else in
// this codebase; `Rational::to_double()` remains "display/estimation only"
// (core/rational.hpp) everywhere outside this one file.
//
// -- Reproducibility: a real distinction from DeterministicPrng --
// DeterministicPrng promises bit-for-bit identical output across
// independent implementations, because its entire definition is
// fixed-width unsigned integer arithmetic with no implementation-defined
// behavior anywhere. This header cannot make that promise and does not
// attempt to: IEEE 754 `double` arithmetic is fully specified per
// operation, but the exact sequence of operations a second implementation
// performs (operation order, intermediate rounding, use of fused
// multiply-add, standard-library transcendental function implementations
// such as `std::log` behind integrate_elapsed_seconds()) is not specified
// by this header closely enough to guarantee identical rounding across
// compilers or platforms. A second, independent implementation of this
// specification -- the writer's or the runtime's -- should reproduce this
// module's output to a small, bounded numerical tolerance, not
// bit-for-bit. Golden-vector comparisons of this header's functions must
// therefore use a documented tolerance-based comparison (for example,
// `std::abs(actual - expected) <= 1e-6` for a seconds-valued result), never
// exact equality. This is a deliberate, weaker guarantee than
// DeterministicPrng's, matched to what floating-point curve integration
// can actually promise.
//
// -- Normalizing a beat-unit-relative BPM to a common rate --
// Tempo (core/tempo.hpp) pairs a BPM Rational with a NoteValue beat unit,
// so two TempoPoints can express the identical musical tempo with
// different numbers: 120 BPM against a quarter-note beat and 60 BPM
// against a half-note beat both mean "a half note takes half a second" --
// the same rate of musical time passing. Interpolating or integrating
// raw, un-normalized BPM numbers across two points with different beat
// units would silently blend two different units together (exactly as
// wrong as averaging a distance in miles with one in kilometers without
// converting first). Every function below normalizes each point's tempo
// to a single common rate, in whole notes per second, BEFORE any
// interpolation or integration touches it:
//
//   note_fraction(beat_unit) = 1 / 2^k, k = 0 for kWhole, 1 for kHalf, ...,
//                              6 for kSixtyFourth (the same power-of-two
//                              convention Duration::base_length() uses,
//                              core/duration.hpp)
//   normalized_rate(point) = point.tempo.bpm() * note_fraction(
//                              point.tempo.beat_unit()) / 60
//                            -- whole notes per second, computed as an
//                               exact Rational and converted to double
//                               only once, immediately before this
//                               header's curve arithmetic uses it.
//
// 120 BPM/quarter and 60 BPM/half both normalize to
// 120 * (1/4) / 60 == 60 * (1/2) / 60 == 1/2 whole note per second,
// confirming the two are interchangeable inputs to every function below.
//
// -- Segment evaluation: kStep, kLinear, kSmooth --
// A segment runs from TempoPoint points[i] to points[i + 1] (or, for the
// lane's final point, indefinitely -- see "The lane's final point" below).
// Let r_i and r_(i+1) be the two endpoints' normalized rates (whole notes
// per second) and let tau in [0, 1] be the exact fraction of the segment's
// Rational duration already traveled (tau = (position - points[i].
// position) / (points[i + 1].position - points[i].position), computed as
// an exact Rational and converted to double only for the formulas below):
//
//   kStep:   rate(tau) = r_i                          (constant)
//   kLinear: rate(tau) = r_i + (r_(i+1) - r_i) * tau   (straight line)
//   kSmooth: a cardinal (Catmull-Rom-family) cubic Hermite spline through
//            r_i and r_(i+1), using the ADJACENT points' rates r_(i-1) and
//            r_(i+2) to shape the tangent at each endpoint so the curve is
//            visually smooth and shape-preserving through the neighboring
//            TempoPoint data -- this is what "auto-shaped from neighboring
//            TempoPoint data" (this phase's product decision; TempoPoint
//            gained no new control-handle fields) means concretely. With
//            tension constant `kTempoCurveTension` (see "Choosing the
//            tension constant" below):
//
//              m_i     = kTempoCurveTension * (r_(i+1) - r_(i-1))
//              m_(i+1) = kTempoCurveTension * (r_(i+2) - r_i)
//              h00(t) = 2t^3 - 3t^2 + 1     h01(t) = -2t^3 + 3t^2
//              h10(t) = t^3 - 2t^2 + t      h11(t) = t^3 - t^2
//              rate(tau) = h00(tau)*r_i + h10(tau)*m_i
//                        + h01(tau)*r_(i+1) + h11(tau)*m_(i+1)
//
//            This is the standard cubic Hermite form of a cardinal spline
//            (Catmull-Rom is the specific case tension == 1/2); it passes
//            exactly through r_i at tau == 0 and r_(i+1) at tau == 1 (the
//            Hermite basis functions satisfy h00(0)==1, h00(1)==0,
//            h01(0)==0, h01(1)==1, and h10/h11 vanish at both ends), which
//            is exactly why evaluating a kSmooth segment at its own
//            boundary position returns the boundary point's own rate
//            exactly, the same as kStep/kLinear do -- the RATE VALUE is
//            therefore always continuous at every shared point, regardless
//            of neighboring segment kinds or spacing.
//
//            What this construction does NOT generally give is a continuous
//            first derivative (matching SLOPE, i.e. d(rate)/d(position)) at
//            a shared point. The tangent `m` above is expressed in
//            per-segment PARAMETER units (tau in [0, 1] over that segment's
//            own Rational duration), so the slope with respect to musical
//            position approaching a shared point from the segment on its
//            left is m_left / (left segment's duration), while the slope
//            approaching it from the segment on its right is
//            m_right / (right segment's duration) -- and m_left == m_right
//            at that point only because both segments share the same pair
//            of neighbor rates in the tangent formula, NOT because the
//            duration scaling matches. These two one-sided slopes coincide
//            -- giving true C1 continuity -- only when both adjacent
//            segments are kSmooth AND have exactly equal Rational duration.
//            Whenever segment lengths differ, or a kSmooth segment neighbors
//            a kStep or kLinear one (whose own slope convention is
//            independent of this tangent formula entirely), the rate curve
//            has a slope discontinuity (a kink) at the shared point -- an
//            accepted, documented property of this construction, not a
//            defect, and not something a caller needs to special-case: the
//            rate value itself, which is what every function in this header
//            actually returns and integrates, remains exact and continuous
//            regardless.
//
// -- Legal control handles --
// A TempoPoint's only "control handles" over its kSmooth shape are its own
// position and normalized rate, plus its two immediate neighbors' -- there
// is no independent tangent, weight, or bias field (Adam's ruling: no
// TempoPoint schema change). The tangent at each endpoint is entirely
// derived from that endpoint's neighbors via the cardinal-spline formula
// above; an author shapes the curve only by adding, moving, or retempoing
// points, never by editing a handle directly. This is the entire "legal
// control handles" surface this phase specifies: none beyond the points
// themselves.
//
// -- Choosing the tension constant --
// kTempoCurveTension == 1/2 is the classic, uniform-parametrization
// Catmull-Rom spline -- not a separate tunable "smoothing amount" but the
// textbook definition of a Catmull-Rom curve itself (Catmull and Rom,
// 1974; the tension-1/2 case is what "Catmull-Rom" conventionally names).
// It is chosen over other tensions or over a chord-length/centripetal
// parametrization (Barry and Goldman, 1988, which avoids Catmull-Rom's
// well-known cusp/self-intersection risk on very unevenly spaced control
// points) because it is the simplest, most widely implemented convention
// for exactly this "auto-shape a smooth curve through a handful of
// author-placed points" use case -- the same reasoning DeterministicPrng's
// doc comment gives for choosing SplitMix64 over a fancier generator: the
// smallest, most standard specification a second implementation can
// transcribe without inventing its own interpretation. Uniform
// parametrization (tau linear in Rational position fraction, not
// chord-length-adjusted) can overshoot the range spanned by two adjacent
// rates when neighboring segments are very unevenly spaced or reverse
// direction sharply -- a known, accepted property of Catmull-Rom splines,
// not a defect here -- which is why every rate this header computes for a
// kSmooth segment is defensively floored at `kMinNormalizedRate` before
// being used as an integration divisor (see "Why kSmooth integrates
// numerically" below); legitimate authored tempi (10-400 BPM,
// whole-note-through-sixty-fourth-note beat units) never come remotely
// close to that floor on their own.
//
// -- The lane's final point --
// A segment's TempoSegmentKind describes how to interpolate TOWARD the
// NEXT point; the lane's final point has none. This header receives only
// `points`, not a lane's declared end (that boundary belongs to
// graphscore/domain/tempo_lane.hpp's TempoLane, a domain-layer type this
// core-layer header cannot depend on) -- so for any position at or past
// the final point, every function below holds that point's normalized
// rate constant, regardless of its stated segment_kind. This is a
// deliberate, documented fallback, not a special case a caller needs to
// avoid: TempoLane::create() already guarantees the lane's declared
// coverage never asks a kLinear/kSmooth final point to interpolate toward
// a target that does not exist, so this fallback only ever activates for
// positions genuinely past every declared point -- exactly the trailing
// constant-rate behavior a kStep segment would already produce there.
//
// -- One-sided (lane-boundary) kSmooth segments --
// The cardinal-spline tangent formulas above need points[i - 1] and
// points[i + 2], which do not exist at the lane's first and last
// interpolated segments. This header uses the standard reflected/mirrored
// endpoint convention: a missing points[i - 1] is synthesized as
// `2 * r_i - r_(i+1)` (the point that would make r_i itself the midpoint
// between the missing neighbor and r_(i+1)), and a missing points[i + 2]
// is synthesized as `2 * r_(i+1) - r_i`, symmetrically. This is the same
// construction most cardinal-spline implementations use at an open curve's
// ends (sometimes called a "clamped" or "reflected" boundary condition);
// it keeps the tangent at a boundary segment proportional to that
// segment's own rate of change rather than requiring a phantom point with
// no data behind it.
//
// -- Integration: elapsed real time between two positions --
// integrate_elapsed_seconds() computes
// integral_from^to (1 / rate(position)) d(position) -- rate is whole notes
// per second, so its reciprocal is seconds per whole note, and integrating
// that reciprocal over a whole-note position range gives elapsed seconds,
// exactly the relationship "faster tempo covers the same musical distance
// in less time" requires. It partitions [from, to] at every segment
// boundary strictly between them (an exact Rational comparison, so a
// query landing exactly on a boundary is never ambiguous) and sums each
// segment's own contribution:
//
//   kStep:   contribution = (delta position) / r_i         -- trivial.
//   kLinear: rate(position) is affine in position, so its reciprocal has
//            the elementary antiderivative
//              integral dp / (A + Bp) = (1/B) * ln(A + Bp),
//            giving, over a sub-range with endpoint rates r_a and r_b and
//            per-whole-note slope B = (r_(i+1) - r_i) / (segment
//            duration):
//              contribution = ln(r_b / r_a) / B   (B != 0)
//              contribution = (delta position) / r_a   (B == 0, degenerate
//                                                        constant-rate
//                                                        case, avoiding a
//                                                        0/0 antiderivative)
//            r_a and r_b are always strictly positive (see "Choosing the
//            tension constant" and Tempo's own 10-400 BPM range), so
//            ln(r_b / r_a) is always well-defined.
//   kSmooth: see "Why kSmooth integrates numerically" immediately below.
//
// -- Why kSmooth integrates numerically, and the fixed quadrature scheme --
// rate(tau) is a cubic polynomial in tau for a kSmooth segment (the
// Hermite form above, expanded), so its reciprocal's antiderivative would
// require locating that cubic's roots (to perform a partial-fraction
// decomposition of 1 / cubic(tau)) -- those roots can be complex, and even
// when real, evaluating the resulting closed form is markedly more
// numerically delicate than the polynomial itself, for a curve whose
// entire purpose is a smooth-sounding auto-shaped visual/audible
// approximation, not an exactly reproducible legal boundary. This header
// instead integrates 1 / rate(tau) with a FIXED, non-adaptive composite
// Simpson's rule, evaluated as an ANTIDERIVATIVE anchored at the segment's
// own start: F(t) = integral from tau == 0 (never from whatever sub-range
// endpoint a particular call happens to request) to tau == t, using
// `kSmoothQuadratureSteps` equal subintervals of [0, t]. A within-segment
// query [tau_a, tau_b] is then answered as F(tau_b) - F(tau_a), NOT as a
// fresh `kSmoothQuadratureSteps`-subinterval quadrature of [tau_a, tau_b]
// itself. This anchoring is deliberate and load-bearing for additivity: an
// earlier version of this scheme quadratured whatever sub-range was asked
// for directly, which is NOT exactly additive across an interior split of
// a single kSmooth segment, because a narrow sub-range and a wide one get
// different effective step sizes for the same fixed subinterval count, so
// integrating [a, c] in one call and integrating [a, b] + [b, c] in two
// calls can measurably disagree (on the order of 1e-7 seconds per split
// for ordinarily spaced authored data) even though both should equal the
// same elapsed time in principle -- and that disagreement compounds under
// REPEATED incremental integration (for example roughly 1e-4 seconds of
// drift per second of playback at a ~1000-block/second callback rate,
// were a caller to integrate one audio block at a time instead of always
// from a fixed anchor). Anchoring every quadrature at tau == 0 instead
// closes this exactly: integrate(a, c) and integrate(a, b) +
// integrate(b, c) both reduce algebraically to F(c) - F(a), so they agree
// up to ordinary floating-point non-associativity (a few ULP), not up to
// a step-size-dependent quadrature error that can be many orders of
// magnitude larger. This mirrors how the kLinear closed form immediately
// above is already exactly additive (log(r_b / r_a) telescopes through any
// intermediate r_mid); kSmooth now shares that property by construction.
// Composite Simpson's rule is chosen over a coarser fixed-step scheme
// (e.g. the trapezoid rule) for its markedly better accuracy per
// evaluation (error O(h^4) rather than O(h^2)) at negligible extra
// implementation cost, and over an ADAPTIVE quadrature (recursive
// subdivision until an error estimate drops below some epsilon)
// specifically because an adaptive scheme's iteration count -- and
// therefore its exact rounding behavior -- can differ between runs,
// platforms, or compilers for numerically identical input, which would
// break this header's own tolerance-based (not adaptive-scheme-dependent)
// reproducibility promise above. `kSmoothQuadratureSteps` is fixed at 32
// (an even number, as composite Simpson's rule requires): a full
// lane-boundary-to-lane-boundary integration this header expects to see in
// ordinary authored data -- tens of subintervals per segment query, not
// thousands -- comfortably exceeds the accuracy that matters for a curve
// whose own shape is already only an auto-shaped smoothing convenience,
// while staying cheap enough that invert_elapsed_seconds() below can
// afford dozens of these integrations per call. Even with this anchoring,
// a caller composing elapsed time across MULTIPLE segments (or multiple
// calls into this header generally) should still always integrate from a
// fixed anchor position -- the node/segment start, or the transport's last
// confirmed position -- rather than accumulating by repeatedly integrating
// short consecutive sub-ranges call over call; this is the safer, always-
// correct pattern regardless of which segment kind is involved, and is
// exactly what integrate_elapsed_seconds() itself does internally when it
// walks multiple segments in one call.
//
// -- Inversion: real time back to musical position --
// invert_elapsed_seconds() answers "which position is reached after
// `elapsed_seconds` of real time from `from`, within [from, curve_end]" --
// the inverse of integrate_elapsed_seconds(). Because rate(position) is
// always strictly positive (see "Choosing the tension constant" above and
// the `kMinNormalizedRate` floor), integrate_elapsed_seconds(points, from,
// x) is a strictly increasing function of x, so this inversion is a
// well-posed one-dimensional root find and this header uses ordinary
// bisection: a FIXED `kTempoInversionIterations` iterations (never
// "until the bracket is smaller than epsilon" -- see the reproducibility
// note above for why an unbounded iteration count is unacceptable here),
// narrowing a [lo, hi] bracket by evaluating
// integrate_elapsed_seconds(points, from, mid) at each step's midpoint
// and comparing it against the target `elapsed_seconds`. The search
// itself is carried out in `double` position space for simplicity and
// speed (dozens of integration calls per invert_elapsed_seconds() call);
// each candidate midpoint is converted to a Rational, via a fixed
// `1 / kInversionQuantizationDenominator`-wide grid (chosen once via
// rounding, never compounded across iterations by repeated Rational
// arithmetic -- see "Why quantize rather than bisect in Rational space"
// below), only where a Rational value is actually required as
// integrate_elapsed_seconds()'s argument. The function returns `from`
// immediately for `elapsed_seconds <= 0` and `curve_end` immediately once
// `elapsed_seconds` reaches or exceeds the total time integrate_
// elapsed_seconds(points, from, curve_end) reports for the whole covered
// range -- both without spending any bisection iterations, since both
// answers are already exact.
//
// -- Why quantize rather than bisect in Rational space --
// Bisecting directly in Rational space -- repeatedly computing
// `mid = (lo + hi) / Rational(2)` via Rational's own exact,
// cross-multiplying arithmetic -- would, after `kTempoInversionIterations`
// repeated divisions, risk a denominator whose growth is not bounded by a
// fixed multiple of the previous one in the worst case (Rational's
// GCD reduction only shrinks a denominator that shares a common factor
// with its accumulated history; two arbitrary fractions' sum or product
// can multiply denominators outright), which is exactly the kind of
// unbounded-growth risk this project's `std::int64_t`-backed Rational
// cannot absorb silently. This header sidesteps that risk entirely by
// never performing Rational arithmetic on a shrinking bracket at all:
// every candidate midpoint is built fresh, once, directly from a `double`
// via `Rational::create(llround(value * kInversionQuantizationDenominator),
// kInversionQuantizationDenominator)` -- a single fixed-denominator
// construction, not an accumulating chain of exact divisions, so its
// numerator's magnitude is bounded by (the position's own magnitude) *
// kInversionQuantizationDenominator regardless of how many bisection
// iterations preceded it.
//
// -- The tolerance, and why it is sufficient --
// `kTempoInversionIterations` is fixed at 52, matching an IEEE 754
// `double`'s 52 explicit mantissa bits: bisecting a bracket further than
// double precision itself can resolve is wasted, deterministic, bounded
// work, never a correctness problem, so 52 is chosen as the point past
// which additional iterations stop meaningfully narrowing the bracket at
// all, rather than being tuned against any particular piece length. As a
// concrete worked bound: even an implausibly long piece spanning
// 10^9 whole notes (far beyond any realistic node) bisected 52 times
// narrows to a bracket width of roughly 10^9 / 2^52 =~ 2.2 * 10^-7 whole
// notes; at the fastest normalized rate this header's own Tempo range
// permits (400 BPM against a whole-note beat unit =~ 6.667 whole notes per
// second), that maps to a worst-case real-time uncertainty of roughly
// 2.2 * 10^-7 / 6.667 =~ 3.3 * 10^-8 seconds -- 33 nanoseconds, several
// orders of magnitude below one sample period even at a 192 kHz sample
// rate (=~ 5.2 microseconds) let alone a typical 44.1/48 kHz rate, and
// therefore far finer than round_to_sample_count() below can distinguish
// at any sample rate a real audio device uses.
// `kInversionQuantizationDenominator` is fixed at 2^20 (1,048,576). The
// binding safety constraint is not simply "the quantized numerator fits in
// std::int64_t" (an earlier version of this argument, corrected below) --
// it is the cross-multiplied PRODUCT Rational::operator<=> actually forms
// when this header's own bisection compares two Rational values it built at
// this fixed denominator D: `num_ * other.den_` computes
// `(position * D) * D == position * D^2`, because a numerator built at a
// fixed power-of-two denominator does not generally reduce (an odd
// numerator never does), so both operands still carry the full D. That
// product must stay below std::int64_t's ~9.2 * 10^18 (2^63) range:
// `position * D^2 < 2^63`.
//
// At D == 2^20, D^2 == 2^40, giving a safe position range of
// `2^63 / 2^40 =~ 2^23 =~ 8.39 * 10^6` whole notes -- vastly larger than
// any realistic node (tens or hundreds of whole notes, not millions) --
// with a grid resolution of `1 / 2^20 =~ 9.54 * 10^-7` whole notes, which
// at the fastest normalized rate this header's own Tempo range permits
// (=~ 6.667 whole notes per second) maps to a worst-case real-time
// uncertainty of roughly 9.54 * 10^-7 / 6.667 =~ 1.43 * 10^-7 seconds --
// still about 37 times finer than one sample period even at a 192 kHz
// sample rate (=~ 5.2 microseconds), let alone a typical 44.1/48 kHz rate,
// and therefore, exactly as the paragraph above already argues for
// kTempoInversionIterations, far finer than round_to_sample_count() below
// can distinguish at any sample rate a real audio device uses.
//
// An earlier version of this module set D to 2^32 and argued only that
// `position * D` -- roughly 10^9 * 2^32 =~ 4.3 * 10^18 -- stayed
// "comfortably inside int64_t's ~9.2 * 10^18 range". That bounded the wrong
// quantity: it ignored that the comparison above squares D, not just
// multiplies by it once, so 2^32 in fact overflowed `std::int64_t` for any
// position magnitude above roughly 0.5 whole notes -- a real, reachable
// signed-integer-overflow abort under this project's asan-ubsan CI preset,
// triggered by this module's own round-trip test on entirely ordinary
// musical input, not an extreme or adversarial one. 2^20 is chosen with
// headroom below the 2^23 ceiling `position * D^2 < 2^63` otherwise
// permits, rather than at that ceiling exactly.
//
// This bound covers only the numerator this module itself manufactures at
// its own fixed denominator D. A caller-supplied `from`/`curve_end`
// Rational that independently carries some other, unrelated large
// denominator remains a separate, pre-existing risk in
// Rational::operator<=>'s cross-multiplication generally -- already
// recorded as accepted-as-designed for non-musical extreme operands
// (docs/plan/M02_HANDOFF.md's Phase 1 entry) -- and is not something this
// module's own quantization choice can or should re-litigate.
//
// -- Integer sample rounding --
// round_to_sample_count() converts an elapsed-seconds `double` and a
// caller-supplied sample rate (a runtime parameter, never a hardcoded
// constant -- docs/plan/README.md: "Process calls provide absolute sample
// position, sample rate/configuration...") into an integer sample count
// using round-half-to-even ("banker's rounding"): the tie-breaking rule
// IEEE 754 itself defaults to (`roundTiesToEven`), and the conventional
// choice in digital signal processing specifically because round-half-up
// introduces a small but systematic upward bias whenever many independent
// values each land exactly on a .5 boundary, while round-half-to-even
// cancels that bias on average. This header implements the rule by hand,
// entirely in terms of the input `double` and ordinary arithmetic/
// comparisons -- deliberately not `std::nearbyint()`/`std::rint()`, which
// depend on the ambient floating-point rounding-mode environment
// (<cfenv>) rather than being a pure function of their arguments alone,
// and this function's own contract ("a pure function, no state") is
// exactly what rules that dependency out. `sample_rate_hz <= 0` has no
// meaningful sample count to compute; rather than divide by a
// non-positive rate, this returns 0, the same documented-harmless
// fail-safe convention DeterministicPrng::next_below() uses for its own
// degenerate `bound == 0` case.
//
// `elapsed_seconds` is an arbitrary caller-supplied `double`, and this
// function guards two more ways that value can misbehave before it ever
// reaches `static_cast<std::int64_t>`, which is undefined behavior for a
// non-finite or out-of-range operand (this project's asan-ubsan CI preset
// runs `-fsanitize=float-cast-overflow`, which catches exactly this). A
// non-finite `elapsed_seconds` (NaN, +infinity, or -infinity --
// `!std::isfinite(elapsed_seconds)`) returns 0, the same fail-safe already
// used for `sample_rate_hz <= 0`. A finite `elapsed_seconds` whose scaled
// value (`elapsed_seconds * sample_rate_hz`) would fall outside the range
// `std::int64_t` can represent is saturated to
// `std::numeric_limits<std::int64_t>::max()` or `::min()` rather than cast,
// so an implausible-but-finite input (for example `1e30` seconds) cannot
// invoke undefined behavior either.
inline constexpr double kTempoCurveTension = 0.5;

inline constexpr double kMinNormalizedRate = 1e-6;

inline constexpr int kSmoothQuadratureSteps = 32;

inline constexpr int kTempoInversionIterations = 52;

inline constexpr std::int64_t kInversionQuantizationDenominator =
    std::int64_t{1} << 20;

// The instantaneous normalized tempo rate, in whole notes per second, at
// `position`, evaluated against whichever segment of `points` governs it
// (see this header's overview above for the exact per-TempoSegmentKind
// formulas and the beat-unit normalization every point is put through
// first). `points` must be non-empty and strictly ordered by position,
// exactly as graphscore/domain/tempo_lane.hpp's TempoLane already
// enforces for any span this function is meant to be called with; this
// function does not re-validate that ordering itself (see this header's
// overview's "Reproducibility" note -- a caller supplying a malformed
// span gets an unspecified, not a crashing, result). Returns std::nullopt
// only when `points` is empty or `position` falls before points.front().
// position -- there is no corresponding upper bound, because this
// function does not know a lane's declared end (see "The lane's final
// point" above); a caller must restrict queries to a lane's own declared
// coverage itself.
[[nodiscard]] std::optional<double> tempo_rate_at(
    std::span<const TempoPoint> points, Rational position);

// Elapsed real time, in seconds, along the tempo curve `points` describes,
// from `from` to `to` (`from <= to`). See this header's overview above for
// the closed-form kStep/kLinear integration and the fixed-step Simpson's
// rule kSmooth integration this walks segment-by-segment between the two
// positions. Returns std::nullopt when `points` is empty, `from > to`, or
// `from` falls before points.front().position -- the same lower-bound
// precondition tempo_rate_at() has, and for the same reason (no known
// upper bound without a lane's declared end); `to` may extend past
// points.back().position, in which case the trailing segment's constant
// rate (see "The lane's final point" above) governs the remainder.
[[nodiscard]] std::optional<double> integrate_elapsed_seconds(
    std::span<const TempoPoint> points, Rational from, Rational to);

// The exact Rational musical position reached after `elapsed_seconds` of
// real time have passed from `from`, clamped to [`from`, `curve_end`].
// `curve_end` is the caller's own upper bound on the range this function
// is allowed to search -- normally graphscore/domain/tempo_lane.hpp's
// TempoLane::end() -- since, unlike tempo_rate_at()/
// integrate_elapsed_seconds() above, inversion genuinely needs a
// well-defined range to search and clamp its answer into. See this
// header's overview above for the fixed-iteration bisection this performs
// and why its tolerance is sufficient. Returns std::nullopt when `points`
// is empty, `from` falls before points.front().position, or
// `curve_end <= from` (an empty or inverted search range); returns `from`
// immediately, with no search performed, when `elapsed_seconds <= 0`, and
// `curve_end` immediately when `elapsed_seconds` already reaches or
// exceeds the total elapsed time across [`from`, `curve_end`].
[[nodiscard]] std::optional<Rational> invert_elapsed_seconds(
    std::span<const TempoPoint> points, Rational from, Rational curve_end,
    double elapsed_seconds);

// Rounds `elapsed_seconds` to the nearest whole sample at `sample_rate_hz`
// (a caller-supplied runtime parameter, in Hz), using round-half-to-even
// tie-breaking -- see this header's overview above for why. A pure
// function of its two arguments only, with no dependency on the ambient
// floating-point environment. `sample_rate_hz <= 0` returns 0; a non-finite
// `elapsed_seconds` (NaN or +/-infinity) also returns 0; a finite
// `elapsed_seconds` that scales outside `std::int64_t`'s representable
// range saturates to that type's max or min rather than overflowing (see
// this header's overview above for all three).
[[nodiscard]] std::int64_t round_to_sample_count(double       elapsed_seconds,
                                                 std::int64_t sample_rate_hz);

}  // namespace graphscore
