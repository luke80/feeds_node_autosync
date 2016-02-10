<?php

/**
 * Implements hook_schema_alter()
 *
 * Modifies the 'node_type' schema, adding a, 'active' toggle and config JSON-encoded string
 *
 * @param schema
 *   Entire schema, used to modify the data returned from the structure
*/
function feeds_node_autosync_schema_alter(&$schema) {
	$schema['node_type']['fields']['auto_sync_toggle'] = array(
			'type' => 'int',
			'not null' => TRUE,
			'default' => 0,
			'description' => 'Toggle setting for auto-sync (feeds) module.',
	);
	$schema['node_type']['fields']['auto_sync_config'] = array(
		'type' => 'blob',
		'size' => 'big',
		'not null' => FALSE,
		'serialize' => TRUE,
		'description' => 'Configuration for auto-sync (feeds) module.',
	);
}

/**
 * Implemented whenever the configuration for a given node type needs to be accessed (recalled or stored)
 *
 * The first argument is always the node_type, the second is optional and will store the given data as the config.
 *
 * @param node_type
 *   The string id of the node type
 * @param config_obj
 *   The settings to store, changes the return type to int of number of rows updated (should be 0 or 1)
 * @param retrieve_importer_configfig_obj
 *   Boolean to turn on additional data containing the configuration of importers
 */
function feeds_node_autosync_config( $node_type, $config_obj = NULL, $retrieve_importer_config = FALSE ) {
	//	This section is for storing the settings data (if $config_obj provided)
	if(!is_null($config_obj)) {
		if(is_string($config_obj)) {	//	If it is a string, we need to get it into the correct "shape" by encoding it into an object, then turning it back into a string.
			$config_str = $config_obj;
			$config_obj = json_decode($config_str);
			$config_str = json_encode($config_obj);
		} else {	//	If it isn't a string, then it get simpler - simply change it into a string.
			$config_str = json_encode($config_obj);
		}
		$num_updated = db_update('node_type')
			->fields(array('auto_sync_config' => $config_str))
			->condition('type', $node_type)
			->execute();
		if($num_updated == 0) {
			return FALSE;
		} else {
			watchdog('feeds_node_autosync',t("Content type @content_type configuration updated.",array("@content_type"=>node_type_get_name($node_type))),NULL,WATCHDOG_DEBUG);
			//return $num_updated;	//	Commented out to allow return object, even when re-setting the config.
		}
	}
	//	recall the existing settings from the database.
	$result = db_select('node_type', 'nt')
		->fields('nt', array('auto_sync_config'))
		->condition('nt.type',$node_type)
		->range(0,1)
		->execute();
	$config_db = $result->fetchAssoc();
	//	if there isn't one - get the default and set that, also load it into the '$result' container.
	if($result->rowCount() == 0 || $config_db == array('auto_sync_config'=>NULL) || $config_db == NULL) {
		module_load_install('feeds_node_autosync');		//	the install script contains the schema. First we load it.
		$config = feeds_node_autosync_schema();		//	Then we get the default schema.
		$config = $config['feeds_node_autosync_config'];	//	 We don't need that root element...
		$config['config'] = $config['default_config'];	//	Then we "copy" the default into place.
		$config_str = json_encode($config);		//	We need the string version to store in the database.
		$num_updated = db_update('node_type')
			->fields(array('auto_sync_config' => $config_str))
			->condition('type', $node_type)
			->execute();		//	Put it in the database!
		$result = db_select('node_type', 'nt')
			->fields('nt', array('auto_sync_config'))
			->condition('nt.type',$node_type)
			->range(0,1)
			->execute();
		$config_db = $result->fetchAssoc();		//	Retrieve it from the database (to ensure that what the function returns is literally what the database contains).
	}
	//	Failure checking.
	if($result->rowCount() > 0) {
		//	Get the database string
		$config_str = $config_db['auto_sync_config'];
		//	Turn it into the object...
		$config_obj = json_decode($config_str);
		if(!is_object($config_obj->config->block_field_edits->value))	//	This should always be an object.
			$config_obj->config->block_field_edits->value = (object) $config_obj->config->block_field_edits->value;
		if($retrieve_importer_config && $config_obj->config->importer->value) {
			//	Retrieve the importer configuration
			$importer = feeds_importer($config_obj->config->importer->value);
			if(is_object($importer) && $importer) {
				$config_obj->config->importer_config = $importer->getConfig();
			}
		}
		//	Return the result!
		return $config_obj;
	} elseif($node_type != "") {
		//	In case of error, let the user know it failed.
		drupal_set_message(t("Failure to store the default configuration for node type %node_type in the database. Configuration not set. Proceed with caution.",array("%node_type"=>$node_type)), 'error', TRUE);
		return $config;
	} else
		return false;
}
