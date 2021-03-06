/*
 * Inkscape::SelectionDescriber - shows messages describing selection
 *
 * Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2004 MenTaLguY
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "document.h"
#include "sp-item-group.h"
#include "xml/repr.h"
#include "util/find-last-if.h"
#include "layer-fns.h"

// TODO move the documentation comments into the .h file

namespace Inkscape {

namespace {

static bool is_layer(SPObject &object) {
    return SP_IS_GROUP(&object) &&
           SP_GROUP(&object)->layerMode() == SPGroup::LAYER;
}

/** Finds the next sibling layer for a \a layer
 *
 *  @returns NULL if there are no further layers under a parent
 */
static SPObject *next_sibling_layer(SPObject *layer) {
    if (layer->parent == nullptr) {
      return nullptr;
    }
    SPObject::ChildrenList &list = layer->parent->children;
    auto l = std::find_if(++list.iterator_to(*layer), list.end(), &is_layer);
    return l != list.end() ? &*l : nullptr;
}

/** Finds the previous sibling layer for a \a layer
 *
 *  @returns NULL if there are no further layers under a parent
 */
static SPObject *previous_sibling_layer(SPObject *layer) {
    using Inkscape::Algorithms::find_last_if;

    SPObject::ChildrenList &list = layer->parent->children;
    auto l = find_last_if(list.begin(), list.iterator_to(*layer), &is_layer);
    return l != list.iterator_to(*layer) ? &*(l) : nullptr;
}

/** Finds the first child of a \a layer
 *
 *  @returns the layer itself if layer has no sublayers
 */
static SPObject *first_descendant_layer(SPObject *layer) {
    while (true) {
        auto first_descendant = std::find_if(layer->children.begin(), layer->children.end(), &is_layer);
        if (first_descendant == layer->children.end()) {
            break;
        }
        layer = &*first_descendant;
    }

    return layer;
}

/** Finds the last (topmost) child of a \a layer
 *
 *  @returns NULL if layer has no sublayers
 */
static SPObject *last_child_layer(SPObject *layer) {
    using Inkscape::Algorithms::find_last_if;

    auto l = find_last_if(layer->children.begin(), layer->children.end(), &is_layer);
    return l != layer->children.end() ? &*l : nullptr;
}

static SPObject *last_elder_layer(SPObject *root, SPObject *layer) {
    using Inkscape::Algorithms::find_last_if;
    SPObject *result = nullptr;

    while ( layer != root ) {
        SPObject *sibling(previous_sibling_layer(layer));
        if (sibling) {
            result = sibling;
            break;
        }
        layer = layer->parent;
    }

    return result;
}

}

/** Finds the next layer under \a root, relative to \a layer in
 *  depth-first order.
 *
 *  @returns NULL if there are no further layers under \a root
 */
SPObject *next_layer(SPObject *root, SPObject *layer) {
    using std::find_if;

    g_return_val_if_fail(layer != NULL, NULL);
    SPObject *result = nullptr;

    SPObject *sibling = next_sibling_layer(layer);
    if (sibling) {
        result = first_descendant_layer(sibling);
    } else if ( layer->parent != root ) {
        result = layer->parent;
    }

    return result;
}


/** Finds the previous layer under \a root, relative to \a layer in
 *  depth-first order.
 *
 *  @returns NULL if there are no prior layers under \a root.
 */
SPObject *previous_layer(SPObject *root, SPObject *layer) {
    using Inkscape::Algorithms::find_last_if;

    g_return_val_if_fail(layer != NULL, NULL);
    SPObject *result = nullptr;

    SPObject *child = last_child_layer(layer);
    if (child) {
        result = child;
    } else if ( layer != root ) {
        SPObject *sibling = previous_sibling_layer(layer);
        if (sibling) {
            result = sibling;
        } else {
            result = last_elder_layer(root, layer->parent);
        }
    }

    return result;
}

/**
*  Creates a new layer.  Advances to the next layer id indicated
 *  by the string "layerNN", then creates a new group object of
 *  that id with attribute inkscape:groupmode='layer', and finally
 *  appends the new group object to \a root after object \a layer.
 *
 *  \pre \a root should be either \a layer or an ancestor of it
 */
SPObject *create_layer(SPObject *root, SPObject *layer, LayerRelativePosition position) {
    SPDocument *document = root->document;

    static int layer_suffix=1;
    gchar *id=NULL;
    do {
        g_free(id);
        id = g_strdup_printf("layer%d", layer_suffix++);
    } while (document->getObjectById(id));

    Inkscape::XML::Document *xml_doc = document->getReprDoc();
    Inkscape::XML::Node *repr = xml_doc->createElement("svg:g");
    repr->setAttribute("inkscape:groupmode", "layer");
    repr->setAttribute("id", id);
    g_free(id);

    if ( LPOS_CHILD == position ) {
        root = layer;
        SPObject *child_layer = Inkscape::last_child_layer(layer);
        if ( NULL != child_layer ) {
            layer = child_layer;
        }
    }

    if ( root == layer ) {
        root->getRepr()->appendChild(repr);
    } else {
        Inkscape::XML::Node *layer_repr = layer->getRepr();
        layer_repr->parent()->addChild(repr, layer_repr);

        if ( LPOS_BELOW == position ) {
            SP_ITEM(document->getObjectByRepr(repr))->lowerOne();
        }
    }

    return document->getObjectByRepr(repr);
}

}

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
