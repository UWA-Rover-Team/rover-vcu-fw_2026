#include "app_types.h"
#include "fsm.h"
#include <zephyr/ztest.h>

ZTEST(fsm, test_init) {
  struct VCUState state;
  zassert_equal(fsm_init(&state), 0, "fsm_init returned error");
  zassert_equal(state.cs, FSM_BOOTING, "cs should be FSM_BOOTING after init");
  zassert_equal(state.ns, FSM_BOOTING, "ns should be FSM_BOOTING after init");
}

ZTEST(fsm, test_fsm_transition_booting) {
  struct VCUState state;
  fsm_init(&state);

  // test all other transitions dont work
  for (int i = 0; i < TRAN_EVT_FAULT_CLEAR + 1; i++) {
    if (i == TRAN_EVT_BOOTED)
      continue;
    fsm_update_state(&state, i);
    zassert_equal(state.ns, FSM_BOOTING, "ns should still be FSM_BOOTING");
  }

  fsm_update_state(&state, TRAN_EVT_BOOTED);
  zassert_equal(state.ns, FSM_STOPPED, "ns should be FSM_STOPPED");
}

ZTEST(fsm, test_fsm_transition_stopped) {
  struct VCUState state;
  fsm_init(&state);
  fsm_update_state(&state, TRAN_EVT_BOOTED);
  state.cs = state.ns; /* commit to STOPPED */
  zassert_equal(state.cs, FSM_STOPPED, "cs should be FSM_STOPPED");

  /* test all other transitions dont work */
  for (int i = 0; i < TRAN_EVT_FAULT_CLEAR + 1; i++) {
    if (i == TRAN_EVT_ENABLE)
      continue;
    fsm_update_state(&state, i);
    zassert_equal(state.ns, FSM_STOPPED, "ns should still be FSM_STOPPED");
  }

  fsm_update_state(&state, TRAN_EVT_ENABLE);
  zassert_equal(state.ns, FSM_IDLE, "ns should be FSM_IDLE");
}

ZTEST(fsm, test_fsm_transition_idle) {
  struct VCUState state;
  fsm_init(&state);
  state.cs = FSM_IDLE;

  /* test all other transitions dont work */
  for (int i = 0; i < TRAN_EVT_FAULT_CLEAR + 1; i++) {
    if (i == TRAN_EVT_RUN)
      continue;
    fsm_update_state(&state, i);
    zassert_equal(state.ns, FSM_IDLE, "ns should still be FSM_IDLE");
  }

  fsm_update_state(&state, TRAN_EVT_RUN);
  zassert_equal(state.ns, FSM_EXECUTE, "ns should be FSM_EXECUTE");
}

ZTEST(fsm, test_fsm_transition_execute) {
  struct VCUState state;
  fsm_init(&state);
  state.cs = FSM_EXECUTE;

  /* test all other transitions dont work */
  for (int i = 0; i < TRAN_EVT_FAULT_CLEAR + 1; i++) {
    if (i == TRAN_EVT_STOP || i == TRAN_EVT_HOLD || i == TRAN_EVT_SUSPEND)
      continue;
    fsm_update_state(&state, i);
    zassert_equal(state.ns, FSM_EXECUTE, "ns should still be FSM_EXECUTE");
  }

  fsm_update_state(&state, TRAN_EVT_STOP);
  zassert_equal(state.ns, FSM_STOPPING, "ns should be FSM_STOPPING");

  state.cs = FSM_EXECUTE; // reset to execute

  fsm_update_state(&state, TRAN_EVT_HOLD);
  zassert_equal(state.ns, FSM_HOLDING, "ns should be FSM_HOLDING");

  state.cs = FSM_EXECUTE; // reset to execute

  fsm_update_state(&state, TRAN_EVT_SUSPEND);
  zassert_equal(state.ns, FSM_SUSPENDING, "ns should be FSM_SUSPENDING");
}

ZTEST(fsm, test_fsm_transition_stopping) {
  struct VCUState state;
  fsm_init(&state);
  state.cs = FSM_STOPPING;

  /* test all other transitions dont work */
  for (int i = 0; i < TRAN_EVT_FAULT_CLEAR + 1; i++) {
    if (i == TRAN_EVT_STOP)
      continue;
    fsm_update_state(&state, i);
    zassert_equal(state.ns, FSM_STOPPING, "ns should still be FSM_STOPPING");
  }

  fsm_update_state(&state, TRAN_EVT_STOP);
  zassert_equal(state.ns, FSM_STOPPED, "ns should be FSM_STOPPED");
}

ZTEST(fsm, test_fsm_transition_holding) {
  struct VCUState state;
  fsm_init(&state);
  state.cs = FSM_HOLDING;

  /* test all other transitions dont work */
  for (int i = 0; i < TRAN_EVT_FAULT_CLEAR + 1; i++) {
    if (i == TRAN_EVT_HOLD)
      continue;
    fsm_update_state(&state, i);
    zassert_equal(state.ns, FSM_HOLDING, "ns should still be FSM_HOLDING");
  }

  fsm_update_state(&state, TRAN_EVT_HOLD);
  zassert_equal(state.ns, FSM_HELD, "ns should be FSM_HELD");
}

ZTEST(fsm, test_fsm_transition_held) {
  struct VCUState state;
  fsm_init(&state);
  state.cs = FSM_HELD;

  /* test all other transitions dont work */
  for (int i = 0; i < TRAN_EVT_FAULT_CLEAR + 1; i++) {
    if (i == TRAN_EVT_RESUME)
      continue;
    fsm_update_state(&state, i);
    zassert_equal(state.ns, FSM_HELD, "ns should still be FSM_HELD");
  }

  fsm_update_state(&state, TRAN_EVT_RESUME);
  zassert_equal(state.ns, FSM_EXECUTE, "ns should be FSM_EXECUTE");
}

ZTEST(fsm, test_fsm_transition_suspending) {
  struct VCUState state;
  fsm_init(&state);
  state.cs = FSM_SUSPENDING;

  /* test all other transitions dont work */
  for (int i = 0; i < TRAN_EVT_FAULT_CLEAR + 1; i++) {
    if (i == TRAN_EVT_SUSPEND)
      continue;
    fsm_update_state(&state, i);
    zassert_equal(state.ns, FSM_SUSPENDING,
                  "ns should still be FSM_SUSPENDING");
  }

  fsm_update_state(&state, TRAN_EVT_SUSPEND);
  zassert_equal(state.ns, FSM_SUSPENDED, "ns should be FSM_SUSPENDED");
}

ZTEST(fsm, test_fsm_transition_suspended) {
  struct VCUState state;
  fsm_init(&state);
  state.cs = FSM_SUSPENDED;

  /* test all other transitions dont work */
  for (int i = 0; i < TRAN_EVT_FAULT_CLEAR + 1; i++) {
    if (i == TRAN_EVT_RESUME)
      continue;
    fsm_update_state(&state, i);
    zassert_equal(state.ns, FSM_SUSPENDED, "ns should still be FSM_SUSPENDED");
  }

  fsm_update_state(&state, TRAN_EVT_RESUME);
  zassert_equal(state.ns, FSM_EXECUTE, "ns should be FSM_EXECUTE");
}

ZTEST_SUITE(fsm, NULL, NULL, NULL, NULL, NULL);
