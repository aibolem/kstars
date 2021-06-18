/***************************************************************************
                  pykstars.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : 2021-06-03
    copyright            : (C) 2021 by Valentin Boettcher
    email                : hiro at protagon.space; @hiro98:tchncs.de
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <pybind11/pybind11.h>
#include "skyobjects/skypoint.h"
#include "skymesh.h"
#include "cachingdms.h"
#include "sqlstatements.h"
#include "catalogobject.h"
#include "catalogsdb.h"
#include <iostream>

using namespace pybind11::literals;
namespace py = pybind11;
namespace pybind11
{
namespace detail
{
template <>
struct type_caster<QString>
{
  public:
    PYBIND11_TYPE_CASTER(QString, _("QString"));

    bool load(handle src, bool)
    {
        try
        {
            value = QString::fromStdString(src.cast<std::string>());
        }
        catch (const py::cast_error &)
        {
            return false;
        }

        return !PyErr_Occurred();
    }

    static handle cast(QString src, return_value_policy /* policy */, handle /* parent */)
    {
        const handle *obj = new py::object(py::cast(src.toUtf8().constData()));
        return *obj;
    }
};
} // namespace detail
} // namespace pybind11

/**
 * @struct Indexer
 * Provides a simple wrapper to generate trixel ids from python code.
 */
struct Indexer
{
    Indexer(int level) : m_mesh{ SkyMesh::Create(level) } {};

    int getLevel() const { return m_mesh->level(); };
    void setLevel(int level) { m_mesh = SkyMesh::Create(level); };

    int getTrixel(double ra, double dec, bool convert_epoch = false) const
    {
        SkyPoint p{ dms(ra), dms(dec) };
        if (convert_epoch)
        {
            p.B1950ToJ2000();
            p = SkyPoint{ p.ra(), p.dec() }; // resetting ra0, dec0
        }

        return m_mesh->index(&p);
    };

    SkyMesh *m_mesh;
};

///////////////////////////////////////////////////////////////////////////////
//                                   PYBIND                                  //
///////////////////////////////////////////////////////////////////////////////

const CatalogObject DEFAULT_CATALOG_OBJECT{};

template <typename T>
T cast_default(const py::object &value, const T &default_value)
{
    try
    {
        return py::cast<T>(value);
    }
    catch (const py::cast_error &)
    {
        return default_value;
    }
}

PYBIND11_MODULE(pykstars, m)
{
    m.doc() = "Thin bindings for KStars to facilitate trixel indexation from python.";

    py::class_<Indexer>(m, "Indexer")
        .def(py::init<int>(), "level"_a,
             "Initializes an `Indexer` with the given `level`.\n"
             "If the level is greater then approx. 10 the initialization can take some "
             "time.")
        .def_property("level", &Indexer::getLevel, &Indexer::setLevel,
                      "Sets the level of the HTMesh/SkyMesh used to index points.")
        .def(
            "get_trixel", &Indexer::getTrixel, "ra"_a, "dec"_a, "convert_epoch"_a = false,
            "Calculates the trixel number from the right ascention and the declination.\n"
            "The epoch of coordinates is assumed to be J2000.\n\n"
            "If the epoch is B1950, `convert_epoch` has to be set to `True`.")
        .def("__repr__", [](const Indexer &indexer) {
            std::ostringstream lvl;
            lvl << indexer.getLevel();
            return "<Indexer level=" + lvl.str() + ">";
        });

    {
        using namespace CatalogsDB;
        py::class_<DBManager>(m, "DBManager")
            .def(py::init([](const std::string &filename) {
                     return new DBManager(QString::fromStdString(filename));
                 }),
                 "filename"_a)
            .def(
                "register_catalog",
                [](DBManager &self, const py::dict &cat) {
                    return self.register_catalog(
                        py::cast<int>(cat["id"]), py::cast<QString>(cat["name"]),
                        py::cast<bool>(cat["mut"]), py::cast<bool>(cat["enabled"]),
                        py::cast<double>(cat["precedence"]),
                        py::cast<QString>(cat["author"]),
                        py::cast<QString>(cat["source"]),
                        py::cast<QString>(cat["description"]),
                        py::cast<int>(cat["version"]), py::cast<QString>(cat["color"]),
                        py::cast<QString>(cat["license"]),
                        py::cast<QString>(cat["maintainer"]));
                },
                "catalog"_a)
            .def("__repr__",
                 [](const DBManager &manager) {
                     return QString("<DBManager filename=\"" + manager.db_file_name() +
                                    "\">");
                 })
            .def("update_catalog_views", &DBManager::update_catalog_views)
            .def("compile_master_catalog", &DBManager::compile_master_catalog)
            .def("dump_catalog", &DBManager::dump_catalog, "catalog_id"_a, "file_path"_a)
            .def("import_catalog", &DBManager::import_catalog, "file_path"_a,
                 "overwrite"_a)
            .def("remove_catalog", &DBManager::remove_catalog, "catalog_id"_a);

        py::register_exception<DatabaseError>(m, "DatabaseError");
    }

    py::enum_<SkyObject::TYPE>(m, "ObjectType", "The types of CatalogObjects",
                               py::arithmetic())
        .value("STAR", SkyObject::STAR)
        .value("CATALOGSTAR", SkyObject::CATALOG_STAR)
        .value("PLANET", SkyObject::TYPE::PLANET)
        .value("OPEN_CLUSTER", SkyObject::TYPE::OPEN_CLUSTER)
        .value("GLOBULAR_CLUSTER", SkyObject::TYPE::GLOBULAR_CLUSTER)
        .value("GASEOUS_NEBULA", SkyObject::TYPE::GASEOUS_NEBULA)
        .value("PLANETARY_NEBULA", SkyObject::TYPE::PLANETARY_NEBULA)
        .value("SUPERNOVA_REMNANT", SkyObject::TYPE::SUPERNOVA_REMNANT)
        .value("GALAXY", SkyObject::TYPE::GALAXY)
        .value("COMET", SkyObject::TYPE::COMET)
        .value("ASTEROID", SkyObject::TYPE::ASTEROID)
        .value("CONSTELLATION", SkyObject::TYPE::CONSTELLATION)
        .value("MOON", SkyObject::TYPE::MOON)
        .value("ASTERISM", SkyObject::TYPE::ASTERISM)
        .value("GALAXY_CLUSTER", SkyObject::TYPE::GALAXY_CLUSTER)
        .value("DARK_NEBULA", SkyObject::TYPE::DARK_NEBULA)
        .value("QUASAR", SkyObject::TYPE::QUASAR)
        .value("MULT_STAR", SkyObject::TYPE::MULT_STAR)
        .value("RADIO_SOURCE", SkyObject::TYPE::RADIO_SOURCE)
        .value("SATELLITE", SkyObject::TYPE::SATELLITE)
        .value("SUPERNOVA", SkyObject::TYPE::SUPERNOVA)
        .value("NUMBER_OF_KNOWN_TYPES", SkyObject::TYPE::NUMBER_OF_KNOWN_TYPES)
        .value("TYPE_UNKNOWN", SkyObject::TYPE::TYPE_UNKNOWN)
        .export_values();

    m.def(
        "get_id",
        [](const py::dict &obj) -> py::bytes {
            return CatalogObject::getId(
                       static_cast<SkyObject::TYPE>(py::cast<int>(obj["type"])),
                       py::cast<double>(obj["ra"]), py::cast<double>(obj["dec"]),
                       cast_default<float>(obj["magnitude"],
                                           DEFAULT_CATALOG_OBJECT.mag()),
                       cast_default<double>(obj["major_axis"],
                                            DEFAULT_CATALOG_OBJECT.a()),
                       cast_default<double>(obj["minor_axis"],
                                            DEFAULT_CATALOG_OBJECT.b()),
                       cast_default<double>(obj["position_angle"],
                                            DEFAULT_CATALOG_OBJECT.pa()),
                       cast_default<float>(obj["flux"], DEFAULT_CATALOG_OBJECT.flux()),
                       py::cast<QString>(obj["name"]),
                       py::cast<QString>(obj["long_name"]),
                       py::cast<QString>(obj["catalog_identifier"]))
                .toStdString();
        },
        "object"_a,
        R"(
        Calculate the id of an object.

        Parameters
        ----------)");

    ///////////////////////////////////////////////////////////////////////////
    //                             Sql Statements                            //
    ///////////////////////////////////////////////////////////////////////////
    auto s = m.def_submodule("sqlstatements");
    {
        using namespace CatalogsDB::SqlStatements;

        s.doc() = "Assorted sql statements to modify the catalog database.";

        s.def("insert_dso", &insert_dso, "catalog_id"_a);
        s.def("create_catalog_table", &create_catalog_table, "catalog_id"_a);

#define ATTR(name)            \
    {                         \
        s.attr(#name) = name; \
    }
        ATTR(create_catalog_list_table);
        ATTR(insert_catalog);
        ATTR(get_catalog_by_id);
        ATTR(all_catalog_view);
        ATTR(master_catalog);
        ATTR(dso_by_name);
#undef ATTR
    }
}