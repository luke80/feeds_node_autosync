<?php

/**
 * Implements hook_form_alter().
 *
 * Modifies the "node_form" form for any form id ending in "_node_form" (per the initial if). Modifies the form element options to remove editability for protected fields per the node_type settings.
 *
 * @param form
 *   The form array, containing the "current" state of the form. Passed by reference so that modifications are carried.
 * @param form_state
 *   A keyed array containing the current state of the form. Passed by reference so that modifications are carried.
 * @param form_id
 *   String representing the name of the form itself. This is the "FORM_ID" part of the name of the function.
 */
function feeds_node_autosync_form_alter( &$form, &$form_state, $form_id ) {
	//	Required: type, with a #value, to match the form_alter function name
	if (isset($form['type']) && isset($form['type']['#value']) && $form['type']['#value'] . '_node_form' == $form_id) {
		$c = feeds_node_autosync_config($form['#node']->type,NULL,TRUE);		//	Retrieve the configuration
		//if($c->config->active->value != 1) return TRUE;	//	This line would eliminate the possibility of using this module simply to disallow edits to specific fields. The form alter function would terminate here, before it began.
		$block_edits = get_object_vars($c->config->block_field_edits->value);	//	Store the field ids to "block"
		$block_edits_nested = array();
		$ignore_keys = array(	//	Some of these make no sense to parse, let's ignore them...
				"#node_edit_form",
				"#attributes",
				"#node",
				"#validate",
				"#submit",
				"#parents",
				"#entity",
		);
		$disable_field_names = array();		//	In the interest of keeping the user informed, collect the human-readable titles for messages later on.
		$fields = field_info_instances('node',$form['#node']->type);
		$remove_items_block_edits = array();
		foreach($block_edits as $field_name) {	//	loop over the fields to block edits upon
			$fii = field_info_instance('node', $field_name, $form['#node']->type)['label'];	//	additional fields are retrievable with this function. However, it does miss some things.
			if($fii) {
				$disable_field_names[] = $fii;
			} elseif($field_name == 'title') {	//	Disabling the titles is still buggy. Not sure why yet.
				$types = node_type_get_types();
				if (isset($types[$form['#node']->type]) && $types[$form['#node']->type]->has_title) {
					$disable_field_names[] = $types[$form['#node']->type]->title_label;
				} else {
					drupal_set_message(t('Failed to block edits on the title. It looks like %content_type doesn\'t have one?',array('%content_type'=>node_type_get_name($form['#node']->type))),'warning');
				}
				unset($types);
			} elseif($field_name == $c->config->block_field_edits->value->{$field_name}) {
				//	get_object_vars was meant to give me an array of exactly this structure, however occasionally something gets mixed up. First we verify.
				if(strpos($field_name,":") > -1) {	//	if the name contains a ":" it means we're targetting a sub-field, referred to here as "nested"!
					$remove_items_block_edits[] = $field_name;	//	remove this item from the structure... later
					$field_parent = substr($field_name,0,strpos($field_name,":"));
					$field_remainder = substr($field_name,strpos($field_name,":")+1);
					if(array_key_exists($field_parent,$fields))
						$field_label = $fields[$field_parent]['label'];
					else
						$field_label = $field_parent;
					$disable_field_names[] = t("@title",array("@title"=>$field_label.":".$field_remainder));
					//	Now build this information in to the block_edits_nested structure
					if(!array_key_exists($field_parent,$block_edits_nested))
						$block_edits_nested[$field_parent] = array();
					$block_edits_nested[$field_parent][$field_remainder] = $field_remainder;
					unset($field_parent);
					unset($field_remainder);
					unset($field_label);
				}
			}
			unset($fii);
		}
		$block_edits = array_diff($block_edits,$remove_items_block_edits);	//	This is that part that removes the thinks from the structure.
		if(count($disable_field_names) > 0) {
			//	Now let the user know some things are disabled by this module.
			drupal_set_message(t("The following fields are managed by a remote source with Feeds Auto-Sync, and can't be modified;<br/>%field_names !exception",array('%field_names'=>implode(", ",$disable_field_names),"!exception"=>((user_permission('fna edit protected fields')?"<br/>You can edit these fields, however they will probably be overwritten by the Auto-sync.":"")))),"warning");
			//	Parse through the form data to add the '#disabled' flag to matching keys.
			_feeds_node_autosync_traverse_form_data("root",$form,$block_edits,$ignore_keys,$block_edits_nested);
			//	Block edits is passed by reference, so it should now be enpty. If not, warn the user.
			if(count($block_edits) > 0 || TRUE) {	//	This isn't an error, it is more of a warning - and hopefully the problem is corrected with the link provided.
				drupal_set_message(t('Something strange has happened. A configured protected field didn\'t exist in this form. Please verify the %content_type is configured properly with Feeds Auto-Sync.<br/><a href="'.base_path().'admin/structure/feeds/@feed_machine_name/mapping">Change the mappings of %feed_human_name (which drives %content_type) by clicking here.</a>',array('@feed_machine_name'=>$c->config->importer->value,'%feed_human_name'=>$c->config->importer_config['name'],'%content_type'=>node_type_get_name($form['#node']->type))),'warning');
			}
		}
		//	Since the feed is enforced to contain this information before the settings are validated, simplify the node edit form by removing extra fetcher options.
		if(array_key_exists('feeds',$form) && array_key_exists('FeedsHTTPFetcher', $form['feeds'])) {
			_feeds_node_autosync_disable_formfield($form['feeds'],NULL,'This field is configured to be protected from edits. You are able to edit it due to elevated permissions.');
			//	The 'feeds' section doesn't need individual parser settings here.
			foreach($form['feeds'] as $key=>$value) {
				if(preg_match("/^Feeds.+Parser\$/",$key) != 0) unset($form['feeds'][$key]);
			}
		}
		unset($c);
		unset($block_edits);
		unset($block_edits_nested);
		unset($ignore_keys);
		unset($disable_field_names);
		unset($fields);
		unset($remove_items_block_edits);
	}
}

/**
 * Implemented by the above form_altar hook.
 *
 * Recursive search through the form array, disabling "blocked edit" fields.
 *
 * @param key
 *   the key of the branch of form currently being parsed.
 * @param form
 *   reference-passed form so this function can change it.
 * @param block_edits
 *   An array containing the names of the form elements to disable. Since each key is removed once processed, this is passed by reference.
 * @param ignore_keys
 *   An array containing the names of keys to ignore (because they are never form elements)
 * @param nested_keys
 *   A multi-dimensional array containing the names of keys, and the sub-keys to match and ignore. The keys are removed once processed, just like block_edits.
 */
function _feeds_node_autosync_traverse_form_data($key,&$form,&$block_edits,$ignore_keys=array(),&$nested_keys=array()) {
	if(!is_numeric($key)) {	//	non-numeric keys are removed once...
		if(in_array($key,$block_edits)) {	//	Has this one been done? (Does it need to be?)
			_feeds_node_autosync_disable_formfield($form,'This field is not editable because it is synced from a different source.','This field is configured to be protected from edits. You are able to edit it due to elevated permissions.');
			$block_edits = array_diff($block_edits, array($key));	//	Remove this from the list (keys processed once)
		}
		if(array_key_exists($key,$nested_keys)) {	//	Is this a nested key we're looking for?
			$nested_ignore_keys = $nested_keys[$key];
			$children = element_children($form);
			foreach($children as $childkey) {
				_feeds_node_autosync_traverse_form_data($key,$form[$childkey],$nested_ignore_keys,$ignore_keys);
			}
			unset($children);
		}
	}
	//	Detect for and execute required recursion (to find all the form members)
	if((!in_array($key, $ignore_keys) || (is_array($form) && is_numeric($key))) && (count($block_edits) > 0 || count($nested_keys) > 0)) {
		if(is_array($form)){
			$children = element_children($form);
			if( is_array($children) && count($children) > 0 )
			foreach($children as $childkey) {
				_feeds_node_autosync_traverse_form_data($childkey,$form[$childkey],$block_edits,$ignore_keys,$nested_keys);
			}
			unset($children);
		}
	}
}

/**
 * Implemented by functions which need to turn off the editability of form fields (E.g. the function above which recurses to look for them)
 *
 * @param formfield
 *   reference-passed form field so this function can change it.
 * @param notallowed_message
 *   A string message to add to the title of the element to give the user information about what's going on
 * @param exceptionally_allowed_message
 *   A string message to add to the title of the element to give the user information about what's going on
 */
function _feeds_node_autosync_disable_formfield(&$formfield, $notallowed_message=NULL, $exceptionally_allowed_message=NULL) {
	//	Perhaps if this key doesn't exist we ought to simply ignore it... but this also would work.
	if(!array_key_exists('#attributes',$formfield))
		$formfield['#attributes'] = array();
	//	Sometimes there aren't already classes... perhaps there are.
	if(!array_key_exists('class',$formfield['#attributes']) || !is_array($formfield['#attributes']['class']))
		$formfield['#attributes']['class'] = array();
	//	This class is picked up by the module stylesheet. Change the styling there, please.
	$formfield['#attributes']['class'][] = "feeds-node-autosync-disabled";
	if(!user_permission('fna edit protected fields')) {	//	Check permissions
		$formfield['#disabled'] = TRUE;	//	Basic no-access flag on the form input.
		if($notallowed_message)
			$formfield['#attributes']['title'] = t($notallowed_message);
	} else { // if(array_key_exists("#attributes",$form)) {	//	If they can edit it, there should still be some level of indication that something is different.
		$formfield['#attributes']['class'][] = 'feeds-node-autosync-adminaccess';	//	This should color it, highlight it, or whatever to give the user the impression that it might not be editable. When they try - they can... but they still shouldn't.
		if($exceptionally_allowed_message)
			$formfield['#attributes']['title'] = t($exceptionally_allowed_message);
	}
}

/**
 * Implements hook_FORM_ID_form_alter().
 *
 * Modifies the "node_type" form. Adds configuration options to the node type for auto-sync of content of this type.
 *
 * @param form
 *   The form array, containing the "current" state of the form.
 * @param form_state
 *   A keyed array containing the current state of the form.
 * @param form_id
 *   String representing the name of the form itself. This is the "FORM_ID" part of the name of the function.
 */
function feeds_node_autosync_form_node_type_form_alter( &$form, &$form_state, $form_id ) {
	//	Create the new tab (appears at the bottom of the form, probably under "Comment settings"
	$form['feeds_auto_sync'] = array(
		 '#type' => 'fieldset',
		 '#title' => t('Feeds Auto-Sync'),
		 '#collapsible' => TRUE,
		 '#collapsed' => TRUE,
		 '#group' => 'additional_settings',
	);
	if(user_access('fna bundle configuration form')) {
		//	Retrieve the configuration object..
		$c = feeds_node_autosync_config($form['#node_type']->type);

		//	Initialize the $form_datas to contain all the requisite data places
		$form_datas = array(
				'config_problems'		=> array(),		//	This array is populated with helpful text to assist the user to configure the feed to work with this module.
				'importers' => array(
					'all'	=> array(),	//	This array is populated with all the form importers found, and holds a boolean value which is true if this importer is valid for auto-syncing the nodes.
					'valid'		=> array(),	//	This array is populated with the form importers that are valid for use.
					'fields'	=> array(),	//	This array is populated with the form fields that are mapped specifically in this importer.
				),
				'importer_mapped_fields'	=> array(),	//	This array is populated with the mapped fields that are configured, and might need to be disabled to prevent modification to fields intended to be driven only by the feed.
				'importer_mapped_fields_messages'	=> array(),	//	This array is populated with messages from mapped fields that are configured
		);
		if($form['#node_type']->module != "" && $form['#node_type']->module != "node") {
			$form_datas['config_problems'][] = t("Sorry, the type needs to be 'node', not %module_name.",array('%module_name',$form['#node_type']->module));
		} else {
			//	The importer needs to be configured for the node type loaded...
			foreach(feeds_importer_load_all() as $importer) {
				$validation_data = feeds_node_autosync_validate_importer($importer->id, $form['#node_type']->type);
				$form_datas['importers']['all'] =		array_merge($form_datas['importers']['all'], $validation_data['all']);
				$form_datas['importers']['valid'] =		array_merge($form_datas['importers']['valid'], $validation_data['valid']);
				$form_datas['importers']['fields'] =	array_merge($form_datas['importers']['fields'], $validation_data['mapped_fields']);
				$form_datas['importer_mapped_fields'] =	array_merge($form_datas['importer_mapped_fields'], $validation_data['mapped_fields']);
				$form_datas['importer_mapped_fields_messages'] =	array_merge($form_datas['importer_mapped_fields_messages'], $validation_data['mapped_fields_messages']);
			}
		}
		if(count($form_datas['importers']['all']) == 0) {	//	First problem to identify. No importers!
			$form_datas['config_problems'][] =	t('<p class="error">You don\'t even have an importer set up yet! Sorry, but you\'ll have to set one up with this content type %content_type first.</p>',array('%content_type'=>node_type_get_name($form['#node_type']->type)));
		} else if(count($form_datas['importers']['valid']) == 0) {	//	Second problem to identify. No valid importers!
			$form_datas['config_problems'][] =	t('<p class="error">Found %num_feeds available feeds, but none passed validation.<br/><br/>Your configured feed needs to have a <em>feed_source</em> set. You do this by setting one of the import fields mapped to <em>feed_source</em>. It also helps to make the data provided function as a sync source for the specific node it creates/updates.</p>',array('%num_feeds'=>count($form_datas['importers']['all'])));
			$form_datas['config_problems'][] = t('<h4>"Semi-valid" feed list with requirements unmet:</h4>');
			foreach($form_datas['importers']['all'] as $key=>$unvalidated_importer) {
				$form_datas['config_problems'][] = t("<em>".$key."</em>");
				if(array_key_exists('fail_messages', $unvalidated_importer)) {
					foreach($unvalidated_importer['fail_messages'] as $fail_message)
						$form_datas['config_problems'][] = $fail_message;
				}
			}
		} else if(count($form_datas['importer_mapped_fields']) == 0) {	//	Third problem to identify. No fields mapped to update in this content type.
			$form_datas['config_problems'][] =	t('<p class="error">It appears that your <em>processor</em> is either missing or is not set up to connect to this content type with any imported columns. Additionally, you are also required to have at least one unique identifier mapped to this content type %content_type, and the processor set to <em>update existing nodes</em>.',array('%content_type'=>node_type_get_name($form['#node_type']->type)));
		}

		//	Verify successful import of nodes with 'FEED_URL' set in db.
		$missing_nodes_error = t('Before setting auto-update on a content type, you need to successfully sync your nodes manually at least once. (No \'feed_source\' data found. This is required to map. Otherwise the individual items won\'t have a location to sync from.) ');
		foreach($form_datas['importers']['valid'] as $importer_name=>$importer) {
			$missing_nodes_error .= "<br/>".l(t('Click here to go import page for "@importer"',array('@importer'=>$importer)),'/import/'.$importer_name);
		}
		$form_datas['nids'] = db_select('node', 'n')
			->fields('n', array('nid'))
			->fields('n', array('type'))
			->condition('n.type', $form['#node_type']->type)
			//	->condition('n.status', 1)	//	Don't require items to be published (why would this be needed?)
			->execute()
			->fetchCol(); // returns an indexed array
		if(count($form_datas['nids']) > 0 && count($form_datas['config_problems']) == 0) {
			$result = db_select('feeds_source', 'fs')
				->fields('fs', array('id'))
				->fields('fs',array('source'))
				->condition('fs.feed_nid', $form_datas['nids'])
				->condition('fs.source', '', $operator = '<>')
				->range(0,1)
				->execute();
			if($result->rowCount() == 0) {
				$form_datas['config_problems'][] = $missing_nodes_error;
			}
		} else if(count($form_datas['nids']) == 0) {
			$form_datas['config_problems'][] = t("No %content_type nodes have been created. ",array("%content_type"=>node_type_get_name($form['#node_type']->type))).$missing_nodes_error;
		}
		unset($form_datas['nids']);
		unset($result);
		unset($missing_nodes_error);

		/*
		 *	Finally! Begin modifications to the form
		*/
		//	If there are errors to display, display them.
		if(count($form_datas['config_problems']) != 0) {
			$form['feeds_auto_sync']['fna_errors']  = array(
					'#markup' => t(implode("<br/>",$form_datas['config_problems'])),
			);
			//	Since the form settings don't matter if they can't work, hide them.
			return true;
		}
		//	Main control setting. Destination: `node_type`.`auto_sync_toggle`
		$form['feeds_auto_sync']['fna_active']  = array(
				'#type' => 'checkbox',
				'#title' => t('Auto-sync this content type?'),
				'#description' => t('With this checked, when items of this type are modified they will trigger a re-import from the configured source.'),
		);
		if($c->config->active->value == 1) {
			$form['feeds_auto_sync']['fna_active']['#attributes'] = array('checked'=>'checked');
		}
		//	Secondary control. Destination: ctools-encoded parameter within `node_type`.`auto_sync_config`
		$form['feeds_auto_sync']['fna_importer']  = array(
				'#type' => 'select',
				'#options' => $form_datas['importers']['valid'],
				'#title' => t('Which importer do you want the auto-sync to use?'),
				'#description' => t('Overridden settings include; deactivate missing items...'),
				'#default_value' =>	t($c->config->importer->value),
		);

		//	Secondary control. Destination: ctools-encoded parameter within `node_type`.`auto_sync_config`
		$form['feeds_auto_sync']['fna_cooldown']  = array(
				'#type' => 'select',
				'#options' => drupal_map_assoc(array(5,10,15,20,25,30,45,60)),
				'#title' => t('Cooldown delay on re-importing nodes.'),
				'#description' => t('This setting has two purposes, first to defeat recursion (which is triggered when an auto-update modifys a node, causing it to re-update), second to make the sync resistant to causing an importer lock if two users edit the same node at the same (ish) time.'),
				'#default_value' =>	$c->config->cooldown->value,
		);
		//	Secondary control. Destination: ctools-encoded parameter within `node_type`.`auto_sync_config`
		$form['feeds_auto_sync']['fna_block_field_edits']  = array(
				'#type' => 'checkboxes',
				'#options' => $form_datas['importer_mapped_fields'],
				'#title' => t('Block edits on these fields'),
				'#description' => t('Check the node members you wish to make un-editable with the node form.'),
				'#default_value' => array_keys(get_object_vars($c->config->block_field_edits->value)),
		);
		//	If the user lacks appropriate permissions, show the field but don't allow changes
		if(!user_access('fna bundle protect fields form')) {
			if(!is_array($form['feeds_auto_sync']['fna_block_field_edits']['#attributes']))
				$form['feeds_auto_sync']['fna_block_field_edits']['#attributes'] = array();
			$form['feeds_auto_sync']['fna_block_field_edits']['#attributes']['disabled'] = 'disabled';
		}
		//	If there are messages, display them.
		if(count($form_datas['importer_mapped_fields_messages']) > 0) {
			$form['feeds_auto_sync']['fna_block_field_edits_messages']  = array(
					'#markup' => implode("<br/>",$form_datas['importer_mapped_fields_messages']),
			);
		}
		//	Add the submission and validation callbacks to the form.
		array_push($form['#submit'], 	'feeds_node_autosync_nodetypeform_data');
		array_push($form['#validate'], 	'feeds_node_autosync_save_node_type_form_validate');
	} else {
		//	Show the message that the user doesn't have sufficient permission
		$form['feeds_auto_sync']['fna_errors']  = array(
				'#markup' => t("You lack permission to configure the Auto-sync settings."),
		);
	}
}

/**
 * Implemented by "feeds_node_autosync_form_node_type_form_alter", by adding this function to that form's "#submit" array.
 *
 * The columns added to the schema and db recieve their data from the form with this function.
 *
 * @param form
 *   The form array, containing the "current" state of the form.
 * @param form_state
 *   A keyed array containing the current state of the form.
 */
function feeds_node_autosync_nodetypeform_data ( $form, &$form_state ) {
	//	The 'fna_active' field is slightly vestigal at this point, but it doesn't hurt much.
	$num_updated = db_update('node_type')
		->fields(array('auto_sync_toggle' => $form_state['values']['fna_active']))
		->condition('type', $form['#node_type']->type)
		->execute();
	//	load the configuration object!
	$c = feeds_node_autosync_config($form['#node_type']->type);
	foreach($form_state['values'] as $form_field=>$form_value) {
		//	This changes the prefixed 'fna_' fields into their appropriate key within the configuration object.
		if(substr($form_field,0,4) == 'fna_') {		//	First - only use the proper prefix...
			$field = substr($form_field,4);		//	The name within the configuration object doesn't need the prefix.
			if(!is_null($form_value)) {		//	Setting values to NULL should delete them (or fail to set them)
				if(is_array($form_value)) {	//	Arrays need to be looped through, this is currently most important for the "uneditable fields" settings.
					$values = array();
					foreach($form_value as $key=>$value) {	//	Build it out!
						if($value != "0") {
							$values[$key] = $value;
						} else {
							//$values[$key] = FALSE;
						}
					}
					$c->config->{$field}->value = $values;	//	It's ready now for prime-time!
				} elseif(is_numeric($form_value)) {
					$c->config->{$field}->value = intval($form_value);	//	Integers are pretty easy. Store it as an int!
				} else
					$c->config->{$field}->value = $form_value;	//	Otherwise don't do anything special.
			}
			unset($field);
		}
	}
	feeds_node_autosync_config($form['#node_type']->type, $c);	//	This saves the config to the database.
	unset($c);
	return array($form, $form_state);	//	This provides the changes to the UI.
}

/**
 * Implemented by "feeds_node_autosync_form_node_type_form_alter", by adding this function to that form's "#validate" array.
 *
 * The columns added to the schema and db are validated pre-save with this function.
 *
 * @param form
 *   The form array, containing the "current" state of the form.
 * @param form_state
 *   A keyed array containing the current state of the form.
 */
function feeds_node_autosync_save_node_type_form_validate ( $form, &$form_state ) {
	if(user_access('fna bundle configuration form')) {	//	Validate that the user has access to the form
		//	Validate 'fna_active'
		if(!is_numeric($form_state['values']['fna_active']))
			$form_state['values']['fna_active'] = intval($form_state['values']['fna_active']);
	
		//	Validate 'fna_importer'
		$validation_data = feeds_node_autosync_validate_importer($form_state['values']['fna_importer'], $form['#node_type']->type);
		if(!in_array($form_state['values']['fna_importer'],array_keys($validation_data['valid']),TRUE)) {
			drupal_set_message('Importer failed validation.','warning',TRUE);
			//dpm($validation_data);
			//dpm($form_state['values']['fna_importer']);
			$form_state['values']['fna_importer'] = null;
		}
	
		//	Validate 'fna_cooldown'
		if(!is_numeric($form_state['values']['fna_cooldown']))
			$form_state['values']['fna_cooldown'] = intval($form_state['values']['fna_cooldown']);
		if($form_state['values']['fna_cooldown'] < 1)
			$form_state['values']['fna_cooldown'] = 1;
		if($form_state['values']['fna_cooldown'] > 60)
			$form_state['values']['fna_cooldown'] = 60;
		
		if(user_access('fna bundle protect fields form')) {
			//	Validate 'fna_block_field_edits'
			//		There's a potential UI frustration here; if the user wants to block edits on a field not mapped in the importer they chose, this will disallow them to do so.
			//		The reason for this is that if the field isn't controlled by the import, blocking edits using the form will render this field useless for most purposes.
			//		I'm considering making this validation rule optional in the module configuration options (which don't exist yet)
			$valid_fields = array();
			foreach($form_state['values']['fna_block_field_edits'] as $field_name=>$field_value) {
				if(in_array($field_name,array_keys($validation_data['mapped_fields']))) {
					$valid_fields[$field_name] = $field_value;
				}
			}
			$form_state['values']['fna_block_field_edits'] = $valid_fields;
			unset($valid_fields);
		} else {
			//	No permission means this value, if provided, should be removed.
			unset($form_state['values']['fna_block_field_edits']);
		}
	} else {
		//	No permission means these values, if provided, should be removed.
		$fields = array('fna_active','fna_importer','fna_cooldown','fna_block_field_edits');
		foreach($fields as $field_name) {
			if(array_key_exists($field_name,$form_state['values'])) {
				unset($form_state['values'][$field_name]);
			}
		}
		unset($fields);
	}
}

/**
 * Implemented by the above _form_alter hook and "feeds_node_autosync_save_node_type_form_validate" by implicit call
 *
 * This function takes an importer name and validates its settings
 *
 * @param importer_id
 *   The string id of the importer
 * @param node_type
 *   The node id string
 *   
 * returns an array indexed with 'all', 'valid', 'mapped_fields' and 'mapped_fields_messages' containing data about the configured feeds.
 */
function feeds_node_autosync_validate_importer($importer_id, $bundle) {
	//	$ret will be returned, these are the keys that should always be there. These structures are merged elsewhere using these keys. The structure here must match the aggregation there.
	$ret = array('all' => array(), 'valid' => array(), 'mapped_fields' => array(), 'mapped_fields_messages'=>array());
	//	Load the importer and the config thereof
	$importer = feeds_importer($importer_id);
	$importer_config = $importer->getConfig();
	$importer_config['id'] = $importer_id;
	unset($importer);	//	Since this is no longer needed, why not delete it?
	//	Check and enforce that the node type (bundle) matches the current, and that the feed is set to 'update existing nodes'.
	//	Also required is at least one field processed as a unique identifier.
	$ret['all'][$importer_config['id']] = array();
	$ret['all'][$importer_config['id']]['FEED_URL_mapped'] = false;
	$ret['all'][$importer_config['id']]['processor_includes_unique'] = false;
	$ret['all'][$importer_config['id']]['fields'] = array();
	$ret['all'][$importer_config['id']]['fields_messages'] = array();
	$ret['all'][$importer_config['id']]['fail_messages'] = array();
	//	This structure is used to define the criteria the mappings will be either validated or invalidated against.
	$required_maps = array(
			'unique' => array(
					'map_required_key' => 'unique',
					'map_required_value' => 1,
					'storage_key' => 'processor_includes_unique',
					'fail_message' => t('You need to map at least one field as an unique marker per record, otherwise it can\'t differentiate between items.'),
			),
			'unique_field' => array(
					'map_required_key' => 'unique',
					'map_required_value' => 1,
					'map_required_field_key_not' => 'guid',
					'storage_key' => 'processor_includes_unique_field',
					'fail_message' => t('You need to map at least one field as an unique marker per record in addition to a GUID, otherwise multiple feeds will result in duplicate records.'),
			),
			'url' => array(
					'map_required_key' => 'target',
					'map_required_value' => 'url',
					'storage_key' => 'feed_url_mapped',
					'fail_message' => t('You need to map the URL for each item, otherwise the item can\'t be synced when edited.'),
			),
			'feed_source' => array(
				'map_required_key' => 'target',
				'map_required_value' => 'feeds_source',
				'storage_key' => 'feed_source_mapped',
				'fail_message' => t('You need to map the feed_source.'),
			),
			/*'not_found' => array(
			 'map_required_key' => 'never_there',
			 'map_required_value' => 'not_set',
			 'storage_key' => 'failtest',
			 'fail_message' => t('Fail test successful.'),
			),*/
	);
	//	This structure defines mapping criteria that will be ignored (as potential uneditable fields).
	$ignored_maps = array(
			'dates' => array(
					'ignore_mode' => 'widget',
					'ignore_match' => array('key_to_match'=>'type','value_to_match'=>'date_text'),
					'user_message' => 'Fields of the %field_name variety can\'t be protected. They lack adequate markers in their structure (back end stuff).',
			),
			'guid' => array(
					'ignore_mode' => 'name',
					'ignore_match' => 'guid',
					'user_message' => 'Fields of the %field_name variety can\'t be protected. They aren\'t presented on the edit form.',
			),
			'feeds_source' => array(
					'ignore_mode' => 'name',
					'ignore_match' => 'feeds_source',
					'user_message' => 'Fields of the %field_name variety can\'t be protected. They aren\'t presented to the edit form.',
			),
			'url' => array(
					'ignore_mode' => 'name',
					'ignore_match' => 'url',
					'user_message' => 'Note; the %field_name field is protected by default, and therefore isn\'t on this list.',
			),
	);
	//	There are a number of basic criteria that invalidate feeds easily.
	if($importer_config['processor']['config']['bundle'] != $bundle) {
		$ret['all'][$importer_config['id']]['fail_messages'][] = t("Improper node type. ('!node_type_found' needs to be '!node_type_required')",array('!node_type_found'=>$importer_config['processor']['config']['bundle'],'!node_type_required'=>$bundle));
	}
	if($importer_config['processor']['config']['update_existing']."" != "2") {
		$ret['all'][$importer_config['id']]['fail_messages'][] = t("Processor not configured to 'Update existing nodes'. ('!update_existing' value not accepted.)",array('!update_existing'=>$importer_config['processor']['config']['update_existing']));
	}
	if($importer_config['processor']['config']['update_non_existent']."" != "skip") {
		$ret['all'][$importer_config['id']]['fail_messages'][] = t("Processor not configured to 'skip non-existent nodes'. ('!update_non_existent' value not accepted.)",array('!update_non_existent'=>$importer_config['processor']['config']['update_non_existent']));
	}
	if($importer_config['processor']['config']['skip_hash_check']."" != "1") {
		$ret['all'][$importer_config['id']]['fail_messages'][] = t("Processor not configured to 'skip hash check'. ('!skip_hash_check' value not accepted.)",array('!skip_hash_check'=>$importer_config['processor']['config']['skip_hash_check']));
	}
	if(count($ret['all'][$importer_config['id']]['fail_messages']) == 0) {
		//	Here is built the array of fields for locking
		//	Also checked here is the existence of a "FEED_URL" on an item level. If the items don't have a "FEED_URL" there is no capacity to auto-sync its data.
		foreach($importer_config['processor']['config']['mappings'] as $map) {
			$field_name = substr($map['target'],0,((strpos($map['target'],":")>-1)?strpos($map['target'],":"):strlen($map['target'])));
			$importer_field = field_info_instance('node',$field_name,$bundle);
			$ignore = FALSE;
			foreach($ignored_maps as $title=>$data) {
				if($importer_field && array_key_exists($data['ignore_mode'],$importer_field) && $data['ignore_mode'] != "widget") {
					$ignore = TRUE;
				} elseif($data['ignore_match'] == $map['target']) {
					$ignore = TRUE;
				} elseif(
						$data['ignore_mode'] == 'widget' && is_array($importer_field) && array_key_exists("widget",$importer_field) && is_array($importer_field['widget']) &&
						array_key_exists($data['ignore_match']['key_to_match'],$importer_field['widget']) &&
						$importer_field['widget'][$data['ignore_match']['key_to_match']] == $data['ignore_match']['value_to_match']
				) {
					$ignore = TRUE;
				} else {
					//	not ignorable
				}
				if($ignore) {
					if($importer_field)
						$label = $importer_field['label'];
					else
						$label = $map['target'];
					$ret['all'][$importer_config['id']]['fields_messages'][] = t($data['user_message'],array('%field_name'=>$label));
					$ret['mapped_fields_messages'][$title] = t($data['user_message'],array('%field_name'=>$label));
				}
			}
			if($importer_field && !$ignore && strpos($map['target'],":") == -1) {	//	This verifies that the mapped field is a field in the content type
				$ret['all'][$importer_config['id']]['fields'][$importer_field['field_name']] = t($importer_field['label']);
				$ret['mapped_fields'][$importer_field['field_name']] = t($importer_field['label']);
			} else if($map['target'] == 'title' && !$ignore) {
				//	Because the fields API doesn't contain the title the bundle doesn't properly show "title" names...
				$types = node_type_get_types();
				if (isset($types[$bundle]) && $types[$bundle]->has_title) {
					$ret['all'][$importer_config['id']]['fields']['title'] = t($types[$bundle]->title_label);
					$ret['mapped_fields']['title'] = t($types[$bundle]->title_label);
				}
			} elseif(!$ignore) {
				$label = (($importer_field) ? t($importer_field['label']): $map['target']);
				if(strpos($map['target'],":") > -1)
					$label .= substr($map['target'],strpos($map['target'],":"));
				$ret['all'][$importer_config['id']]['fields'][$map['target']] = $label;
				$ret['mapped_fields'][$map['target']] = $label;
			}
			foreach($required_maps as $required_fieldname => $requirement_parameters) {
				//	Complicated match criteria!
				if(
					array_key_exists($requirement_parameters['map_required_key'], $map) &&	//	if required key
					$map[$requirement_parameters['map_required_key']] == $requirement_parameters['map_required_value'] &&	//	and required value
					(	//	and
						!array_key_exists('map_required_field_key_not',$requirement_parameters) ||	//	there isn't one it can't be
						$requirement_parameters['map_required_field_key_not'] != $map['target']		//	or is the one it needs to be
					)
				) {
					$ret['all'][$importer_config['id']][$requirement_parameters['storage_key']] = TRUE;	//	Then set the storage key thing to TRUE
				}
			}
			unset($importer_field);	//	Since this is no longer needed, why not delete it?
		}
		$passed_requirements = TRUE;
		$fail_messages = "";
		//	Check each required map and attach the fail message if it fails validation.
		foreach($required_maps as $required_fieldname => $requirement_parameters) {
			if(!array_key_exists($requirement_parameters['storage_key'], $ret['all'][$importer_config['id']])) {
				$passed_requirements = FALSE;
				$fail_messages .= "<div>".$requirement_parameters['fail_message']."</div>";
			}
		}
		//	If this importer had a mapped "FEED_URL" and passed all validation, then add it to the list of form_importers!
		if($passed_requirements) {
			$ret['valid'][$importer_config['id']] = format_string("!importer_description [!importer_name]", array('!importer_description'=>$importer_config['description'],'!importer_name'=>$importer_config['name']));
		} else {
			$ret['all'][$importer_config['id']]['fail_messages'][] = $fail_messages;
		}
		unset($fail_messages);
		unset($passed_requirements);
	}
	unset($required_maps);	//	Since this is no longer needed, why not delete it?
	unset($importer_config);	//	Since this is no longer needed, why not delete it?
	return $ret;
}
