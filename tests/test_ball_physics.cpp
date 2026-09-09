// Physics checks for src/game/ball_physics.cpp, run standalone: the module has
// no SDL / render dependency on purpose, so it needs nothing but a compiler.
//
//   clang++ -std=c++17 -O2 -Isrc src/game/ball_physics.cpp \
//           tests/test_ball_physics.cpp -o build/test_ball_physics
//   ./build/test_ball_physics
//
// Exit status is 0 when every check passes. These are not smoke tests: each one
// asserts something ball-physics-explained.md actually claims, so a regression
// in the model fails a named check rather than just looking wrong on screen.

#include <game/ball_physics.hpp>
#include <cstdio>
#include <cmath>

using namespace ballphys;
static int fails = 0;
static void check(bool ok, const char* what, const char* detail = "") {
    printf("  [%s] %s %s\n", ok ? " OK " : "FAIL", what, detail);
    if (!ok) ++fails;
}

int main() {
    Table tb;
    const float surf = tb.height + kBallRadius;

    printf("\n== 1. Rebote vertical: restitucion 0.95 ==\n");
    {
        BallState b; b.pos = vec3(0,0,0); b.vel = vec3(0,0,0); b.spin = vec3(0,0,0);
        b.pos = vec3(0.0f, tb.height + 0.30f, 0.5f);
        Prediction p; process_hit(b, tb, p);
        // pico despues del primer rebote
        float y_peak = 0.0f; bool seen_bounce = false;
        for (int i = 0; i < p.queue.count(); ++i) {
            if (p.queue.at(i).bounce) seen_bounce = true;
            else if (seen_bounce) y_peak = std::fmax(y_peak, p.queue.at(i).pos.y);
        }
        float h0 = 0.30f, h1 = y_peak - tb.height;
        char d[128]; snprintf(d, sizeof d, "h0=%.3f h1=%.3f ratio=%.3f (ideal %.3f)",
                              h0, h1, h1/h0, kRestitution*kRestitution);
        check(seen_bounce, "rebota", "");
        check(h1/h0 > 0.75f && h1/h0 < 0.91f, "altura ~ e^2 (con drag algo menor)", d);
    }

    printf("\n== 2. Impulso del rebote: topspin acelera, backspin frena ==\n");
    {
        // Se llama a resolve_bounce_impulse con la MISMA velocidad de llegada y
        // solo se cambia el spin, para aislar el impulso tangencial. Comparar
        // trayectorias completas no sirve: el Magnus previo cambia la velocidad
        // de impacto y tapa el efecto que se quiere medir.
        auto dvz = [&](vec3 spin) {
            BallState b;
            b.pos = vec3(0.0f, tb.height + kBallRadius, 0.5f);
            b.vel = vec3(0.0f, -2.5f, -4.0f);
            b.spin = spin;
            float before = b.vel.z;
            resolve_bounce_impulse(b, tb);
            return b.vel.z - before;
        };
        float d_none = dvz(vec3(0,0,0));
        float d_top  = dvz(vec3(-600.0f, 0, 0));
        float d_back = dvz(vec3(+600.0f, 0, 0));
        char d[190]; snprintf(d, sizeof d, "dvz none=%+.2f top=%+.2f back=%+.2f (negativo = el rebote empuja hacia adelante)",
                              d_none, d_top, d_back);
        check(d_top < 0.0f, "topspin: el rebote empuja hacia adelante", d);
        check(d_back > 0.0f, "backspin: el rebote frena", "");
        check(d_top < d_none && d_none <= d_back, "orden top < none <= back", "");

        // El spin tambien se transfiere: el rebote debe sangrar topspin, no crearlo.
        BallState b; b.pos = vec3(0.0f, tb.height + kBallRadius, 0.5f);
        b.vel = vec3(0.0f, -2.5f, -4.0f); b.spin = vec3(-600.0f, 0, 0);
        resolve_bounce_impulse(b, tb);
        char d2[128]; snprintf(d2, sizeof d2, "spin.x %.1f -> %.1f", -600.0f, b.spin.x);
        check(b.spin.x > -600.0f, "el rebote reduce el topspin (no lo amplifica)", d2);
    }

    printf("\n== 3. Magnus: topspin hunde, backspin flota ==\n");
    {
        auto landing_z = [&](vec3 spin) {
            BallState b; b.pos = vec3(0.0f, tb.height + 0.30f, 1.30f);
            b.vel = vec3(0.0f, 1.8f, -4.5f); b.spin = spin;
            Prediction p; process_hit(b, tb, p);
            for (int i = 0; i < p.queue.count(); ++i)
                if (p.queue.at(i).bounce && p.queue.at(i).pos.z < 0.0f) return p.queue.at(i).pos.z;
            return -99.0f;
        };
        float z_none = landing_z(vec3(0,0,0));
        float z_top  = landing_z(vec3(-300.0f,0,0));
        float z_back = landing_z(vec3(+300.0f,0,0));
        char d[160]; snprintf(d, sizeof d, "z none=%.3f top=%.3f back=%.3f (menos negativo = mas corto)",
                              z_none, z_top, z_back);
        check(z_none > -90.0f && z_top > -90.0f && z_back > -90.0f, "los tres aterrizan en la mesa", d);
        check(z_top > z_none, "topspin aterriza mas corto", "");
        check(z_back < z_none, "backspin aterriza mas largo", "");
    }

    printf("\n== 4. Sidespin curva lateralmente ==\n");
    {
        auto landing_x = [&](vec3 spin) {
            BallState b; b.pos = vec3(0.0f, tb.height + 0.35f, 1.30f);
            b.vel = vec3(0.0f, 1.5f, -5.0f); b.spin = spin;
            Prediction p; process_hit(b, tb, p);
            for (int i = 0; i < p.queue.count(); ++i)
                if (p.queue.at(i).bounce) return p.queue.at(i).pos.x;
            return -99.0f;
        };
        float x_none  = landing_x(vec3(0,0,0));
        float x_left  = landing_x(vec3(0,+300.0f,0));
        float x_right = landing_x(vec3(0,-300.0f,0));
        char d[160]; snprintf(d, sizeof d, "x none=%.3f (+Y spin)=%.3f (-Y spin)=%.3f", x_none, x_left, x_right);
        check(std::fabs(x_none) < 1e-3f, "sin spin no se desvia", d);
        check(x_left < -0.02f && x_right > 0.02f, "spin lateral curva a lados opuestos", "");
    }

    printf("\n== 5. Clasificacion de resultado ==\n");
    {
        // tiro a la red: bajo y lento
        BallState net; net.pos = vec3(0.0f, tb.height + 0.12f, 0.60f);
        net.vel = vec3(0,0.0f,-5.0f); net.spin = vec3(0,0,0);
        Prediction pn; process_hit(net, tb, pn);
        check((pn.outcome & kOutcomeNet) != 0, "tiro bajo -> NET", outcome_name(pn.outcome));

        // tiro bueno
        BallState in; in.pos = vec3(0.0f, tb.height + 0.35f, 1.20f);
        in.vel = vec3(0,1.8f,-5.0f); in.spin = vec3(-300.0f,0,0);
        Prediction pi; process_hit(in, tb, pi);
        check((pi.outcome & kOutcomeIn) != 0, "tiro con topspin -> IN", outcome_name(pi.outcome));

        // tiro largo
        BallState out; out.pos = vec3(0.0f, tb.height + 0.35f, 1.20f);
        out.vel = vec3(0,2.5f,-9.0f); out.spin = vec3(0,0,0);
        Prediction po; process_hit(out, tb, po);
        check((po.outcome & kOutcomeOut) != 0, "tiro largo -> OUT", outcome_name(po.outcome));
    }

    printf("\n== 6. Cola: interpolacion y purga ==\n");
    {
        BallState b; b.pos = vec3(0.0f, tb.height + 0.35f, 1.20f);
        b.vel = vec3(0,1.8f,-5.0f); b.spin = vec3(-300.0f,0,0);
        Prediction p; process_hit(b, tb, p);
        char d[128]; snprintf(d, sizeof d, "%d muestras, t en [%.3f, %.3f]",
                              p.queue.count(), p.queue.min_t(), p.queue.max_t());
        check(p.queue.count() > 2 && p.queue.count() <= PredictQueue::kCapacity, "cola dentro de capacidad", d);

        bool sorted = true;
        for (int i = 1; i < p.queue.count(); ++i)
            if (p.queue.at(i).t <= p.queue.at(i-1).t) sorted = false;
        check(sorted, "muestras ordenadas por t", "");

        BallState s;
        check(!p.queue.sample_at(p.queue.min_t() - 0.1f, s), "t antes de MinT -> falla", "");
        check(!p.queue.sample_at(p.queue.max_t() + 0.1f, s), "t despues de MaxT -> falla", "");

        float mid = 0.5f * (p.queue.min_t() + p.queue.max_t());
        check(p.queue.sample_at(mid, s), "interpola en el medio", "");

        p.queue.pop_expired(mid);
        BallState s2;
        check(p.queue.sample_at(mid, s2), "sigue interpolable tras pop_expired", "");
        char d2[128]; snprintf(d2, sizeof d2, "dif = %.6f m", magnitude(s2.pos - s.pos));
        check(magnitude(s2.pos - s.pos) < 1e-4f, "pop_expired no cambia el resultado", d2);
    }

    printf("\n== 7. Autoaim: dobla hacia el target y respeta el tope ==\n");
    {
        BallState b; b.pos = vec3(0.0f, tb.height + 0.35f, 1.20f);
        b.vel = vec3(0,1.8f,-5.0f); b.spin = vec3(-300.0f,0,0);
        Prediction p; process_hit(b, tb, p);
        int idx = find_nearest_hittable(p, vec3(0.0f, tb.height + 0.2f, -1.0f));
        check(idx >= 0, "hay ventana golpeable", "");

        Prediction q = p;
        vec3 target = q.queue.at(idx > 0 ? idx : 1).pos + vec3(0.5f, 0.0f, 0.0f);
        vec3 before = q.queue.at(idx > 0 ? idx : 1).pos;
        FudgeParams fp = fudge_for(Stroke::Sot);
        apply_autoaim(q, target, fp, 1.0f);
        vec3 after = q.queue.at(idx > 0 ? idx : 1).pos;
        float moved = magnitude(after - before);
        char d[128]; snprintf(d, sizeof d, "movio %.3f m, tope %.2f m", moved, fp.max_dist);
        check(moved > 0.01f, "el autoaim mueve la trayectoria", d);
        check(moved <= fp.max_dist + 1e-4f, "no excede MaxFudgeDist", "");
        check(magnitude(q.queue.at(0).pos - p.queue.at(0).pos) < 1e-6f, "el punto de golpe no se mueve", "");
    }

    printf("\n%s  (%d fallos)\n\n", fails ? "=== HAY FALLOS ===" : "=== TODO OK ===", fails);
    return fails ? 1 : 0;
}
