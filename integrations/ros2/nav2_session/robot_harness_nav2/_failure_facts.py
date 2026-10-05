"""Required native facts for the fixed planner-failed/controller-unsent branch."""
from ._scoped import identity


def failed_children(fact, context):
    if (fact['event'] != 'navigation_bt_closed' or fact['scope_id'] != context['scope_id']
            or not all(fact[k] is True for k in ('bt_worker_joined', 'endpoint_sealed',
                                               'tree_idle', 'leaf_proof_complete'))
            or not context['bt_requested_ns'] <= fact['steady_ns'] <= context['bt_received_ns']):
        raise ValueError('missing or foreign sealed BT proof')
    rows = fact['children']
    if (len(rows) != 2 or {r['action'] for r in rows} != {'compute_path_to_pose', 'follow_path'}
            or any(r['scope_id'] != context['scope_id'] for r in rows)):
        raise ValueError('incomplete or foreign child set')
    planner, controller = (next(r for r in rows if r['action'] == name)
                          for name in ('compute_path_to_pose', 'follow_path'))
    if (planner['kind'] != 'terminal' or planner['terminal'] != 'failed'
            or not identity(planner['child_uuid']) or controller['kind'] != 'not_submitted'
            or controller['child_uuid'] or controller['terminal']):
        raise ValueError('failed/never-submitted child proof is unknown')
    return planner['child_uuid']


def controller_sealed(fact, context):
    if (fact['event'] != 'controller_unsubmitted_sealed' or fact['scope_id'] != context['scope_id']
            or fact['generation'] != context['generation'] or fact['worker_callbacks'] != 0
            or not all(fact[k] is True for k in ('server_idle', 'endpoint_sealed'))
            or not context['controller_seal_requested_ns'] <= fact['steady_ns'] <= context['controller_seal_received_ns']):
        raise ValueError('unsubmitted native controller not positively sealed')
