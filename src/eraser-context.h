#ifndef SP_ERASER_CONTEXT_H_SEEN
#define SP_ERASER_CONTEXT_H_SEEN

/*
 * Handwriting-like drawing mode
 *
 * Authors:
 *   Mitsuru Oka <oka326@parkcity.ne.jp>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * The original dynadraw code:
 *   Paul Haeberli <paul@sgi.com>
 *
 * Copyright (C) 1998 The Free Software Foundation
 * Copyright (C) 1999-2002 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 * Copyright (C) 2008 Jon A. Cruz
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include "common-context.h"

#define SP_ERASER_CONTEXT(obj) ((SPEraserContext*)obj)
#define SP_IS_ERASER_CONTEXT(obj) (dynamic_cast<const SPEraserContext*>((const SPEventContext*)obj))

#define ERC_MIN_PRESSURE      0.0
#define ERC_MAX_PRESSURE      1.0
#define ERC_DEFAULT_PRESSURE  1.0

#define ERC_MIN_TILT         -1.0
#define ERC_MAX_TILT          1.0
#define ERC_DEFAULT_TILT      0.0

class SPEraserContext : public SPCommonContext {
public:
	SPEraserContext();
	virtual ~SPEraserContext();

	static const std::string prefsPath;

	virtual void setup();
	virtual gint root_handler(GdkEvent* event);

	virtual const std::string& getPrefsPath();
};

#endif // SP_ERASER_CONTEXT_H_SEEN

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
