// S5 load test: POST /jobs at 10, then 100, then 500 virtual users (VUs).
// Each VU loops: send one job, check it was created, send the next (no think time),
// so the request rate is whatever the server can sustain at that concurrency.
//
// Start the server with the rate limiter off (every VU shares one IP, D-024):
//   JOBQUEUE_RATE_LIMIT_RPS=0 make start
// then:
//   make bench
import http from 'k6/http';
import { check } from 'k6';

const BASE_URL = __ENV.BASE_URL || 'http://localhost:8080';

// One scenario per load level, run back to back, so the summary reports each level apart.
const LEVELS = [10, 100, 500];
const HOLD = __ENV.HOLD || '30s';
// A request slower than TIMEOUT counts as failed. Each level gets GAP_S after its hold to
// finish in-flight requests (gracefulStop) before the next starts; GAP_S > TIMEOUT, so no
// iteration is cut off uncounted and levels never overlap.
const TIMEOUT = '10s';
const GAP_S = 15;

const scenarios = {};
const thresholds = { http_req_failed: ['rate<0.01'] };
LEVELS.forEach((vus, i) => {
    scenarios[`vus_${vus}`] = {
        executor: 'constant-vus',
        vus,
        duration: HOLD,
        startTime: `${i * (parseInt(HOLD) + GAP_S)}s`,
        gracefulStop: `${GAP_S}s`,
    };
    // PLAN S5 target, per level. A failed threshold still prints the full summary.
    thresholds[`http_req_duration{scenario:vus_${vus}}`] = ['p(95)<50'];
    thresholds[`http_reqs{scenario:vus_${vus}}`] = ['count>=0'];  // only to print req count
});

export const options = {
    scenarios,
    thresholds,
    summaryTrendStats: ['avg', 'min', 'med', 'p(90)', 'p(95)', 'p(99)', 'max'],
};

const body = JSON.stringify({ payload: '{}' });  // instant fake work (D-015)
const params = { headers: { 'Content-Type': 'application/json' }, timeout: TIMEOUT };

export default function () {
    const res = http.post(`${BASE_URL}/jobs`, body, params);
    check(res, { 'status is 201': (r) => r.status === 201 });
}
