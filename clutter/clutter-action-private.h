#ifndef __CLUTTER_ACTION_PRIVATE_H__
#define __CLUTTER_ACTION_PRIVATE_H__

#include "clutter-action.h"
#include "clutter-event.h"

G_BEGIN_DECLS

void            _clutter_action_handle_event            (ClutterAction      *action,
                                                         const ClutterEvent *event);

G_END_DECLS

#endif /* __CLUTTER_ACTION_PRIVATE_H__ */
