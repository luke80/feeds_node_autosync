<?php
/**
 * Implemented by functions that need to read the configuration objects stored in the config column of the database.
 *
 * This function takes a variable name loads it from ctools
 *
 * @param content_type
 *   The string content_type which stores the ctools variable
 */
function feeds_node_autosync_conf($content_type) {
	ctools_include('export');
	$result = ctools_export_load_object('feeds_node_autosync_config', 'content_types', array($content_type));
	if (isset($result[$content_type])) {
		return $result[$content_type];
	}
}

/**
 * Implemented by functions that need to write the configuration objects stored in the config column of the database.
 *
 * This function takes a variable name loads it from ctools
 *
 * @param content_type
 *   The string content_type which stores the ctools variable
 */
function feeds_node_autosync_conf_save($content_type) {
	ctools_include('export');
	$result = ctools_export_load_object('feeds_node_autosync_config', 'content_types', array($content_type));
	if (isset($result[$content_type])) {
		return $result[$content_type];
	}
}

/**
 * Implements 'load callback' for conf exportables.
 */
function feeds_node_autosync_conf_load($name) {
	ctools_include('export');
	$result = ctools_export_load_object('feeds_node_autosync_config', 'content_types', array($content_type));
	if (isset($result[$name])) {
		return $result[$name];
	}
}

/**
 * Save a single conf.
 */
function feeds_node_autosync_config_save(&$conf) {
	$update = (isset($conf->oid) && is_numeric($conf->oid)) ? array('oid') : array();
	return drupal_write_record('feeds_node_autosync_config', $conf, $update);
}

/**
 * Implementation of hook_ctools_plugin_api().
 *
 * Tell CTools that we support the default_mymodule_presets API.
 */
function feeds_node_autosync_ctools_plugin_api($owner, $api) {
	if ($owner == 'feeds_node_autosync' && $api == 'default_feeds_node_autosync_presets') {
		return array('version' => 1);
	}
}

/**
 * Implementation of hook_default_feeds_node_autosync_preset().
 *
 * Provide a couple of default presets.
 */
function feeds_node_autosync_default_feeds_node_autosync_preset() {
	$export = array();

	$preset = new stdClass;
	$preset->api_version = 1;
	$preset->name = 'feeds_node_autosync_default_preset';
	$preset->admin_title = 'Default preset';
	$preset->mydata = 'x';
	$export['feeds_node_autosync_default_preset'] = $preset;

	return $export;
}
